/* splash.c - boot / loading splash screen.   /sbin/splash [label]
 * Draws a logo, a title, a status label, a spinner and a sliding progress bar straight onto /dev/fb0
 * and keeps animating until $MINE_UIREADY (/run/ui.ready) appears - the UI creates that file just
 * before it shows its first frame - or until MINE_SPLASH_MAX seconds (default 15) have passed.
 * Customise:  MINE_SPLASH_TITLE (default "My OS")   /etc/mine/splash.ppm (binary P6 image, replaces
 * the generated logo).   Only the spinner and bar areas are redrawn per frame, so it is cheap. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include "font16.h"

struct ui { int W,H,S; uint32_t *px; };

static void fill(struct ui*u,int x,int y,int w,int h,uint32_t c){
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x+w>u->W) w=u->W-x;
  if(y+h>u->H) h=u->H-y;
  for(int j=0;j<h;j++){ uint32_t*p=u->px+(size_t)(y+j)*u->W+x; for(int i=0;i<w;i++) p[i]=c; }
}
static void disc(struct ui*u,int cx,int cy,int r,uint32_t c){
  for(int y=-r;y<=r;y++){ int dx=0; while((dx+1)*(dx+1)+y*y<=r*r) dx++;
    if(y*y<=r*r) fill(u,cx-dx,cy+y,2*dx+1,1,c); }
}
static void rrect(struct ui*u,int x,int y,int w,int h,int r,uint32_t c){
  fill(u,x+r,y,w-2*r,h,c); fill(u,x,y+r,w,h-2*r,c);
  disc(u,x+r,y+r,r,c); disc(u,x+w-r-1,y+r,r,c); disc(u,x+r,y+h-r-1,r,c); disc(u,x+w-r-1,y+h-r-1,r,c);
}
static int tw(const char*s,int sc){ return (int)strlen(s)*8*sc; }
static void text(struct ui*u,int x,int y,int sc,uint32_t c,const char*s){
  for(;*s;s++,x+=8*sc){ int ch=(unsigned char)*s; if(ch<32||ch>126) ch='?';
    const unsigned char*g=FONT[ch-32];
    for(int r=0;r<16;r++) for(int b=0;b<8;b++) if(g[r]&(0x80>>b)) fill(u,x+b*sc,y+r*sc,sc,sc,c); }
}
static void ctext(struct ui*u,int cx,int y,int sc,uint32_t c,const char*s){ text(u,cx-tw(s,sc)/2,y,sc,c,s); }

/* optional logo: binary PPM (P6, maxval 255), nearest-neighbour scaled to fit maxw x maxh */
static int ppm_logo(struct ui*u,const char*path,int cx,int cy,int maxw,int maxh){
  FILE*f=fopen(path,"rb"); if(!f) return 0;
  int w,h,mv; if(fscanf(f,"P6 %d %d %d",&w,&h,&mv)!=3||mv!=255||w<1||h<1||w>4096||h>4096){ fclose(f); return 0; }
  fgetc(f);
  unsigned char*d=malloc((size_t)w*h*3); if(!d||fread(d,3,(size_t)w*h,f)!=(size_t)w*h){ free(d); fclose(f); return 0; } fclose(f);
  int sw=w,sh=h; if(sw>maxw){ sh=sh*maxw/sw; sw=maxw; } if(sh>maxh){ sw=sw*maxh/sh; sh=maxh; }
  for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ unsigned char*p=d+((size_t)(y*h/sh)*w+(x*w/sw))*3;
    int px=cx-sw/2+x, py=cy-sh/2+y; if(px>=0&&px<u->W&&py>=0&&py<u->H) u->px[(size_t)py*u->W+px]=(p[0]<<16)|(p[1]<<8)|p[2]; }
  free(d); return 1;
}

static const int CS[12][2]={{1000,0},{866,500},{500,866},{0,1000},{-500,866},{-866,500},{-1000,0},{-866,-500},{-500,-866},{0,-1000},{500,-866},{866,-500}};
static struct { int cx,cy,R,r, bx,by,bw,bh; } L;   /* spinner + bar geometry */
#define BG 0x101820

static void draw_static(struct ui*u,const char*title,const char*label){
  int W=u->W,H=u->H; fill(u,0,0,W,H,BG);
  int cx=W/2, ls=W/4, ly=H*22/100;
  if(!ppm_logo(u,"/etc/mine/splash.ppm",cx,ly+ls/2,W*55/100,H*22/100)){
    rrect(u,cx-ls/2,ly,ls,ls,ls/5,0x2D5BD0);
    char m[2]={title[0]?title[0]:'M',0}; int s2=ls/32; if(s2<2) s2=2; ctext(u,cx,ly+ls/2-8*s2,s2,0xFFFFFF,m);
  }
  int ts=W/110; if(ts<2) ts=2; while(tw(title,ts)>W*9/10 && ts>1) ts--;
  ctext(u,cx,ly+ls+H/30,ts,0xFFFFFF,title);
  int ss=u->S; ctext(u,cx,ly+ls+H/30+16*ts+H/40,ss,0xA0B0C0,label);
  L.R=W/14; L.r=W/110>3?W/110:3; L.cx=cx; L.cy=H*70/100;
  L.bw=W*60/100; L.bh=u->S*3; L.bx=(W-L.bw)/2; L.by=H*88/100;
  fill(u,L.bx,L.by,L.bw,L.bh,0x1E2A3A);
}
static void draw_dynamic(struct ui*u,int frame){
  fill(u,L.cx-L.R-L.r-2,L.cy-L.R-L.r-2,2*(L.R+L.r+2),2*(L.R+L.r+2),BG);
  static const uint32_t shade[5]={0xFFFFFF,0x9DB4F0,0x6A8BE0,0x4A6FD0,0x243457};
  int head=(frame/2)%12;
  for(int k=0;k<12;k++){ int age=(head-k+12)%12; uint32_t c=shade[age<4?age:4];
    disc(u,L.cx+L.R*CS[k][0]/1000,L.cy+L.R*CS[k][1]/1000,L.r,c); }
  fill(u,L.bx,L.by,L.bw,L.bh,0x1E2A3A);
  int chunk=L.bw/4, pos=(frame*L.bw/90)%(L.bw+chunk)-chunk;
  int x0=pos<0?0:pos, x1=pos+chunk>L.bw?L.bw:pos+chunk; if(x1>x0) fill(u,L.bx+x0,L.by,x1-x0,L.bh,0x2D5BD0);
}

#ifndef HOST_TEST
static uint32_t pk(const struct fb_var_screeninfo*v,uint32_t c){
  uint32_t r=(c>>16)&255,g=(c>>8)&255,b=c&255;
  int rl=v->red.length,gl=v->green.length,bl=v->blue.length,ro=v->red.offset,go=v->green.offset,bo=v->blue.offset;
  if(v->bits_per_pixel==32&&(rl==0||gl==0||bl==0)){ rl=gl=bl=8; ro=16; go=8; bo=0; }
  if(v->bits_per_pixel==16&&(rl==0||gl==0||bl==0)){ rl=5; gl=6; bl=5; ro=11; go=5; bo=0; }
  return ((r>>(8-rl))<<ro)|((g>>(8-gl))<<go)|((b>>(8-bl))<<bo);
}
static void blit(uint8_t*mem,struct fb_var_screeninfo*v,struct fb_fix_screeninfo*f,struct ui*u,int y0,int y1){
  int bpp=v->bits_per_pixel/8, W=u->W<(int)v->xres?u->W:(int)v->xres;
  if(y0<0) y0=0;
  if(y1>u->H) y1=u->H;
  for(int y=y0;y<y1&&y<(int)v->yres;y++){
    size_t off=(size_t)(y+v->yoffset)*f->line_length; if(off+(size_t)W*bpp>f->smem_len) break;
    uint8_t*row=mem+off; uint32_t*src=u->px+(size_t)y*u->W;
    if(bpp==4) for(int x=0;x<W;x++) ((uint32_t*)row)[x]=pk(v,src[x]);
    else if(bpp==2) for(int x=0;x<W;x++) ((uint16_t*)row)[x]=(uint16_t)pk(v,src[x]);
    else for(int x=0;x<W;x++){ uint32_t c=src[x]; row[x*3]=c&255; row[x*3+1]=(c>>8)&255; row[x*3+2]=(c>>16)&255; }
  }
}
int main(int argc,char**argv){
  const char*label=argc>1?argv[1]:"starting";
  const char*title=getenv("MINE_SPLASH_TITLE")?getenv("MINE_SPLASH_TITLE"):"My OS";
  const char*ready=getenv("MINE_UIREADY")?getenv("MINE_UIREADY"):"/run/ui.ready";
  int maxs=getenv("MINE_SPLASH_MAX")?atoi(getenv("MINE_SPLASH_MAX")):15;
  if(!access(ready,F_OK)) return 0;
  int fd=-1; for(int i=0;i<50&&fd<0;i++){ fd=open("/dev/fb0",O_RDWR); if(fd<0) usleep(100000); }
  if(fd<0) return 1;
  struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
  if(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f)) return 1;
  uint8_t*mem=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(mem==MAP_FAILED) return 1;
  struct ui u={.W=v.xres,.H=v.yres}; u.S=u.W>=1000?3:(u.W>=600?2:1); u.px=calloc((size_t)u.W*u.H,4);
  draw_static(&u,title,label); draw_dynamic(&u,0); blit(mem,&v,&f,&u,0,u.H);
  time_t t0=time(NULL);
  for(int frame=1; access(ready,F_OK) && time(NULL)-t0<maxs; frame++){
    draw_dynamic(&u,frame);
    blit(mem,&v,&f,&u,L.cy-L.R-L.r-2,L.cy+L.R+L.r+3); blit(mem,&v,&f,&u,L.by,L.by+L.bh);
    v.yoffset=0; v.activate=FB_ACTIVATE_NOW; ioctl(fd,FBIOPAN_DISPLAY,&v);
    usleep(66000);
  }
  return 0;
}
#else   /* PC preview: ./splash_host out.ppm [frame] [label] */
int main(int argc,char**argv){
  struct ui u={.W=800,.H=1280,.S=2}; u.px=calloc((size_t)u.W*u.H,4);
  draw_static(&u,getenv("MINE_SPLASH_TITLE")?getenv("MINE_SPLASH_TITLE"):"My OS",argc>3?argv[3]:"starting");
  draw_dynamic(&u,argc>2?atoi(argv[2]):7);
  FILE*f=fopen(argv[1],"wb"); fprintf(f,"P6\n%d %d\n255\n",u.W,u.H);
  for(size_t i=0;i<(size_t)u.W*u.H;i++){ unsigned char c[3]={u.px[i]>>16,u.px[i]>>8,u.px[i]}; fwrite(c,1,3,f);} fclose(f); return 0; }
#endif
