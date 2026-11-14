#!/usr/bin/env python3
"""Build an Android dynamic-partition 'super' image with ONE logical partition ("system").
usage: make_super.py rootfs.ext4 out_prefix [--device-size BYTES]
 writes out_prefix.raw.img (truncated raw, flash from sector 0) and out_prefix.sparse.img (fastboot)"""
import sys, struct, hashlib, argparse, os

SECT=512; RESERVED=4096; GEO_SIZE=4096
MAX_META=65536; SLOTS=1; ALIGN=1<<20; LBS=4096

def sha(b): return hashlib.sha256(b).digest()
def cstr(s,n): b=s.encode(); assert len(b)<n; return b+b'\0'*(n-len(b))

def build(rootfs_path, device_size):
    data=open(rootfs_path,'rb').read()
    psize=(len(data)+4095)//4096*4096
    meta_end=RESERVED+2*GEO_SIZE+2*MAX_META*SLOTS
    first_sector=((meta_end+ALIGN-1)//ALIGN*ALIGN)//SECT
    # tables
    parts=struct.pack('<36sIIII',cstr('system',36),1,0,1,1)                 # READONLY, extent 0, group 1
    exts =struct.pack('<QIQI',psize//SECT,0,first_sector,0)                  # linear, super sector, blockdev 0
    groups=struct.pack('<36sIQ',cstr('default',36),0,0)+struct.pack('<36sIQ',cstr('main',36),0,psize)
    bdevs=struct.pack('<QIIQ36sI',first_sector,ALIGN,0,device_size,cstr('super',36),0)
    tables=parts+exts+groups+bdevs
    off=0; desc=b''
    for t,esz in ((parts,52),(exts,24),(groups,48),(bdevs,64)):
        desc+=struct.pack('<III',off,len(t)//esz,esz); off+=len(t)
    HDR=128
    def hdr(chk): return struct.pack('<IHHI32sI32s',0x414C5030,10,0,HDR,chk,len(tables),sha(tables))+desc
    h=hdr(b'\0'*32); h=hdr(sha(h))
    assert len(h)==HDR
    meta=(h+tables); assert len(meta)<=MAX_META
    meta+=b'\0'*(MAX_META-len(meta))
    g=struct.pack('<II32sIII',0x616c4467,52,b'\0'*32,MAX_META,SLOTS,LBS)
    g=struct.pack('<II32sIII',0x616c4467,52,sha(g),MAX_META,SLOTS,LBS)
    g+=b'\0'*(GEO_SIZE-len(g))
    head=bytearray(first_sector*SECT)
    head[RESERVED:RESERVED+GEO_SIZE]=g
    head[RESERVED+GEO_SIZE:RESERVED+2*GEO_SIZE]=g
    p=RESERVED+2*GEO_SIZE
    for _ in range(2):                      # primary region then backup region
        for s in range(SLOTS): head[p:p+MAX_META]=meta; p+=MAX_META
    raw=bytes(head)+data+b'\0'*(psize-len(data))
    return raw

def sparse(raw, total_bytes):
    BS=4096; assert total_bytes%BS==0 and len(raw)%BS==0
    chunks=[]; nraw=len(raw)//BS
    chunks.append(struct.pack('<HHII',0xCAC1,0,nraw,12+len(raw))+raw)
    ndc=(total_bytes-len(raw))//BS
    if ndc: chunks.append(struct.pack('<HHII',0xCAC3,0,ndc,12))
    hdr=struct.pack('<IHHHHIIII',0xED26FF3A,1,0,28,12,BS,total_bytes//BS,len(chunks),0)
    return hdr+b''.join(chunks)

if __name__=='__main__':
    ap=argparse.ArgumentParser(); ap.add_argument('rootfs'); ap.add_argument('prefix')
    ap.add_argument('--device-size',type=int,default=4<<30,help='size recorded in LP metadata for the super block device')
    a=ap.parse_args()
    raw=build(a.rootfs,a.device_size)
    open(a.prefix+'.raw.img','wb').write(raw)
    open(a.prefix+'.sparse.img','wb').write(sparse(raw,len(raw)))
    print('raw',len(raw),'device_size(meta)',a.device_size)
