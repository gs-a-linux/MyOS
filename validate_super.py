import struct,hashlib,sys
def sha(b): return hashlib.sha256(b).digest()
raw=open(sys.argv[1],'rb').read()
# --- geometry (primary + backup), as liblp ReadPrimaryGeometry/ReadBackupGeometry
for name,off in (('primary',4096),('backup',8192)):
    blk=raw[off:off+4096]
    magic,ssz,chk,maxsz,slots,lbs=struct.unpack('<II32sIII',blk[:52])
    assert magic==0x616c4467,'geometry magic'
    assert ssz==52,'struct size'
    assert sha(blk[:8]+b'\0'*32+blk[40:ssz])==chk,'geometry checksum'
    assert slots>=1 and maxsz%512==0 and maxsz>0
    assert blk[ssz:]==b'\0'*(4096-ssz),'unrecognized fields'
    print(f'geometry {name}: ok  max_size={maxsz} slots={slots} lbs={lbs}')
# --- metadata, primary slot 0 and backup slot 0
for name,base in (('primary',12288),('backup',12288+maxsz*slots)):
    m=raw[base:base+maxsz]
    magic,major,minor,hsz,hchk,tsz,tchk=struct.unpack('<IHHI32sI32s',m[:80])
    assert magic==0x414C5030 and major==10 and minor<=2,'header magic/version'
    assert hsz==(128 if minor<2 else 256),'header size'
    hdr=bytearray(m[:hsz]); hdr[12:44]=b'\0'*32
    assert sha(bytes(hdr))==hchk,'header checksum'
    desc=struct.unpack('<12I',m[80:128])
    tables=m[hsz:hsz+tsz]; assert sha(tables)==tchk,'tables checksum'
    names=['partitions','extents','groups','block_devices']; sizes=[52,24,48,64]
    ent={}
    for i,(n,esz) in enumerate(zip(names,sizes)):
        o,cnt,es=desc[i*3:i*3+3]; assert es==esz,f'{n} entry size'; assert o+cnt*es<=tsz,f'{n} bounds'
        ent[n]=[tables[o+k*es:o+(k+1)*es] for k in range(cnt)]
    print(f'metadata {name}: ok v{major}.{minor}')
P=[struct.unpack('<36sIIII',e) for e in ent['partitions']]
E=[struct.unpack('<QIQI',e) for e in ent['extents']]
G=[struct.unpack('<36sIQ',e) for e in ent['groups']]
B=[struct.unpack('<QIIQ36sI',e) for e in ent['block_devices']]
c=lambda b:b.split(b'\0')[0].decode()
for p in P:
    assert p[1]&~0xF==0,'attribute set'
    print('partition',c(p[0]),'attrs',p[1],'group',c(G[p[4]][0]))
    for e in E[p[2]:p[2]+p[3]]:
        assert e[1]==0 and e[3]<len(B)
        start=e[2]*512; print(f'  extent: {e[0]} sectors @ sector {e[2]} -> dm-linear on {c(B[e[3]][4])}')
        fs=raw[start:start+4096]; print('  ext4 magic @1080:',raw[start+1080:start+1082].hex(),'(53ef expected)')
        assert raw[start+1080:start+1082]==b'\x53\xef'
        assert (e[2]+e[0])*512<=len(raw)
print('block device:',c(B[0][4]),'declared size',B[0][3],'first_logical_sector',B[0][0])
