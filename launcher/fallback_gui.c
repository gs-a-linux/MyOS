/* PID 1 for a Halium rootfs: draws a tiny touch GUI on /dev/fb0. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "font16.h"

static int g_crash=0;
static const char* log_path(void){ const char*p=getenv("MINE_LOG"); return p?p:"/run/app.log"; }
#define BITS_PER_LONG (sizeof(long)*8)
#define TESTBIT(a,b) (((a)[(b)/BITS_PER_LONG] >> ((b)%BITS_PER_LONG)) & 1)

enum { B_NONE=-1, B_TAP=0, B_REBOOT=1, B_OFF=2, B_COUNT=3 };
struct rect { int x,y,w,h; };

struct ui {
  int W,H,S;
  uint32_t *px;                 /* RGB888 back buffer */
  struct rect btn[B_COUNT];
  int taps, down, pressed;      /* pressed = button index under finger or -1 */
  int tx,ty,rx,ry;              /* last touch (screen / raw) */
  char l_fb[128], l_kern[128], l_touch[128], l_sup[128];
  long up;
};

static void fill(struct ui *u,int x,int y,int w,int h,uint32_t c){
  if (x<0){w+=x;x=0;} if (y<0){h+=y;y=0;}
  if (x+w>u->W) w=u->W-x;
  if (y+h>u->H) h=u->H-y;
  for (int j=0;j<h;j++){ uint32_t *p=u->px+(size_t)(y+j)*u->W+x; for(int i=0;i<w;i++) p[i]=c; }
}
static int tw(const char*s,int sc){ return (int)strlen(s)*8*sc; }
static void text(struct ui *u,int x,int y,int sc,uint32_t c,const char*s){
  for (;*s;s++,x+=8*sc){
    int ch=(unsigned char)*s; if (ch<32||ch>126) ch='?';
    const unsigned char*g=FONT[ch-32];
    for (int r=0;r<16;r++) for (int b=0;b<8;b++) if (g[r]&(0x80>>b)) fill(u,x+b*sc,y+r*sc,sc,sc,c);
  }
}
static void ctext(struct ui *u,struct rect r,int sc,uint32_t c,const char*s){
  text(u,r.x+(r.w-tw(s,sc))/2,r.y+(r.h-16*sc)/2,sc,c,s);
}
static int hit(const struct rect*r,int x,int y){ return x>=r->x&&x<r->x+r->w&&y>=r->y&&y<r->y+r->h; }

static void layout(struct ui *u){
  int W=u->W,H=u->H,S=u->S;
  int bw=W*3/4, bh=H/9, bx=(W-bw)/2;
  u->btn[B_TAP]=(struct rect){bx, H*57/100, bw, bh};
  int sw=(bw-W/25)/2, by=H-bh-H/20;
  u->btn[B_REBOOT]=(struct rect){bx, by, sw, bh*3/4>16*S+8?bh*3/4:16*S+8};
  u->btn[B_OFF]=(struct rect){bx+sw+W/25, by, sw, u->btn[B_REBOOT].h};
}

static void render(struct ui *u){
  int W=u->W,H=u->H,S=u->S; char buf[128];
  fill(u,0,0,W,H,0x101820);
  int tb=16*S+W/25; fill(u,0,0,W,tb,0x2D5BD0);
  text(u,W/30,(tb-16*S)/2,S,0xFFFFFF,"super.img GUI demo");
  /* card */
  int cy=tb+H/25, ch=H*22/100; fill(u,W/20,cy,W-W/10,ch,0x1E2A3A);
  int big=S*3; while (tw("Hello World!",big)>W-W/8 && big>1) big--;
  struct rect hr={W/20,cy+ch/6,W-W/10,16*big};
  ctext(u,hr,big,0xFFFFFF,"Hello World!");
  fill(u,W/8,hr.y+16*big+S*4,W-W/4,S*2,0x2D5BD0);
  struct rect sr={W/20,hr.y+16*big+S*12,W-W/10,16*S};
  ctext(u,sr,S,0xA0B0C0,"running as PID 1 from the system partition");
  /* info */
  int y=cy+ch+H/40, lh=16*S+S*3, x=W/20;
  snprintf(buf,sizeof buf,"uptime: %lds",u->up);               text(u,x,y,S,0xA0B0C0,buf); y+=lh;
  text(u,x,y,S,0xA0B0C0,u->l_kern); y+=lh;
  text(u,x,y,S,0xA0B0C0,u->l_fb);   y+=lh;
  text(u,x,y,S,0xA0B0C0,u->l_touch);y+=lh;
  text(u,x,y,S,0xA0B0C0,u->l_sup);y+=lh;
  /* buttons */
  const char *names[B_COUNT]={"","Reboot","Power off"};
  uint32_t col[B_COUNT]={0x2E9E5B,0xC98A1B,0xB03A3A}, colp[B_COUNT]={0x1B6B3C,0x8A5E10,0x7A2626};
  for (int i=0;i<B_COUNT;i++){
    struct rect r=u->btn[i]; uint32_t c=(u->pressed==i)?colp[i]:col[i];
    fill(u,r.x,r.y,r.w,r.h,c);
    if (i==B_TAP){ snprintf(buf,sizeof buf,"TAP ME  (%d)",u->taps); ctext(u,r,S+S/2?S+1:S,0xFFFFFF,buf); }
    else ctext(u,r,S,0xFFFFFF,names[i]);
  }
  if (u->down) { fill(u,u->tx-S*5,u->ty-S*1,S*10,S*2,0xFFFF00); fill(u,u->tx-S*1,u->ty-S*5,S*2,S*10,0xFFFF00); }
}

static void render_crash(struct ui *u){
  int W=u->W,H=u->H,S=u->S;
  fill(u,0,0,W,H,0x1E0E0E);
  int tb=16*S+W/25; fill(u,0,0,W,tb,0xB03A3A);
  text(u,W/30,(tb-16*S)/2,S,0xFFFFFF,"your app exited - retrying");
  static char buf[16384]; size_t n=0; FILE*f=fopen(log_path(),"r");
  if(f){ fseek(f,0,SEEK_END); long sz=ftell(f); long st=sz>(long)sizeof buf-1?sz-(long)sizeof buf+1:0; fseek(f,st,SEEK_SET); n=fread(buf,1,sizeof buf-1,f); fclose(f);} buf[n]=0;
  for(size_t i=0;i<n;i++) if(buf[i]!='\n' && (unsigned char)buf[i]<32) buf[i]=' ';
  int lh=16*S+2, maxl=(H-tb-H/10)/lh, maxc=(W-W/12)/(8*S);
  char *ls[512]; int nl=0; for(char*p=buf;*p&&nl<512;){ ls[nl++]=p; char*e=strchr(p,'\n'); if(!e) break; *e=0; p=e+1; }
  while(nl>0&&!ls[nl-1][0]) nl--;
  int first=nl>maxl?nl-maxl:0, y=tb+H/40;
  if(!nl) text(u,W/24,y,S,0xFFB070,"(no output captured)");
  for(int i=first;i<nl;i++,y+=lh){ char line[200]; snprintf(line,sizeof line,"%.*s",maxc<199?maxc:199,ls[i]); text(u,W/24,y,S,0xFFB070,line); }
}

/* ---------- framebuffer ---------- */
struct fbdev { int fd; uint8_t *mem; struct fb_var_screeninfo v; struct fb_fix_screeninfo f; int ok; };

static uint32_t pk(const struct fb_var_screeninfo*v,uint32_t c){
  uint32_t r=(c>>16)&255,g=(c>>8)&255,b=c&255;
  int rl=v->red.length,gl=v->green.length,bl=v->blue.length,ro=v->red.offset,go=v->green.offset,bo=v->blue.offset;
  if (v->bits_per_pixel==32 && (rl==0||gl==0||bl==0)) { rl=gl=bl=8; ro=16; go=8; bo=0; }
  if (v->bits_per_pixel==16 && (rl==0||gl==0||bl==0)) { rl=5; gl=6; bl=5; ro=11; go=5; bo=0; }
  return ((r>>(8-rl))<<ro)|((g>>(8-gl))<<go)|((b>>(8-bl))<<bo);
}
static void blit(struct fbdev*d,struct ui*u){
  int bpp=d->v.bits_per_pixel/8; int W=u->W<(int)d->v.xres?u->W:(int)d->v.xres;
  for (int y=0;y<u->H && y<(int)d->v.yres;y++){
    size_t off=(size_t)(y+d->v.yoffset)*d->f.line_length;
    if (off+(size_t)W*bpp>d->f.smem_len) break;          /* never write past the mapping */
    uint8_t *row=d->mem+off;
    uint32_t *src=u->px+(size_t)y*u->W;
    if (bpp==4) for(int x=0;x<W;x++) ((uint32_t*)row)[x]=pk(&d->v,src[x]);
    else if (bpp==2) for(int x=0;x<W;x++) ((uint16_t*)row)[x]=(uint16_t)pk(&d->v,src[x]);
    else for(int x=0;x<W;x++){ uint32_t c=src[x]; row[x*3]=c&255; row[x*3+1]=(c>>8)&255; row[x*3+2]=(c>>16)&255; }
  }
  d->v.yoffset=0; d->v.activate=FB_ACTIVATE_NOW;
  ioctl(d->fd,FBIOPAN_DISPLAY,&d->v);
}
static int fb_open(struct fbdev*d){
  d->fd=open("/dev/fb0",O_RDWR); if (d->fd<0) return -1;
  if (ioctl(d->fd,FBIOGET_VSCREENINFO,&d->v)||ioctl(d->fd,FBIOGET_FSCREENINFO,&d->f)){close(d->fd);return -1;}
  d->mem=mmap(0,d->f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,d->fd,0);
  if (d->mem==MAP_FAILED){close(d->fd);return -1;}
  ioctl(d->fd,FBIOBLANK,FB_BLANK_UNBLANK); d->ok=1; return 0;
}

/* ---------- touch ---------- */
struct touch { int fd; int minx,maxx,miny,maxy; int mt; };
static int find_touch(struct touch*t){
  char path[64];
  for (int i=0;i<32;i++){
    snprintf(path,sizeof path,"/dev/input/event%d",i);
    int fd=open(path,O_RDONLY|O_NONBLOCK); if (fd<0) continue;
    unsigned long abs_[(ABS_MAX+BITS_PER_LONG)/BITS_PER_LONG]; memset(abs_,0,sizeof abs_);
    if (ioctl(fd,EVIOCGBIT(EV_ABS,sizeof abs_),abs_)<0){close(fd);continue;}
    int mt=TESTBIT(abs_,ABS_MT_POSITION_X)&&TESTBIT(abs_,ABS_MT_POSITION_Y);
    int st=TESTBIT(abs_,ABS_X)&&TESTBIT(abs_,ABS_Y);
    if (!mt&&!st){close(fd);continue;}
    struct input_absinfo ax,ay;
    if (ioctl(fd,EVIOCGABS(mt?ABS_MT_POSITION_X:ABS_X),&ax)<0||ioctl(fd,EVIOCGABS(mt?ABS_MT_POSITION_Y:ABS_Y),&ay)<0||ax.maximum<=ax.minimum){close(fd);continue;}
    t->fd=fd;t->mt=mt;t->minx=ax.minimum;t->maxx=ax.maximum;t->miny=ay.minimum;t->maxy=ay.maximum; return 0;
  }
  return -1;
}

static void kmsg(const char*s){ int f=open("/dev/kmsg",O_WRONLY); if(f>=0){ dprintf(f,"<6>gui_init: %s\n",s); close(f);} }

static void backlight(void){
  const char *c[]={"/sys/class/leds/lcd-backlight/brightness","/sys/class/leds/lcd-backlight/max_brightness",0};
  char mx[16]="255"; int f=open(c[1],O_RDONLY); if(f>=0){int n=read(f,mx,sizeof mx-1); if(n>0)mx[n]=0; close(f);}
  f=open(c[0],O_WRONLY); if(f>=0){write(f,mx,strlen(mx));close(f);}
  DIR*d=opendir("/sys/class/backlight");
  if(d){struct dirent*e; while((e=readdir(d))){ if(e->d_name[0]=='.')continue; char p[256],m[16]="255";
    snprintf(p,sizeof p,"/sys/class/backlight/%s/max_brightness",e->d_name); f=open(p,O_RDONLY); if(f>=0){int n=read(f,m,sizeof m-1); if(n>0)m[n]=0; close(f);}
    snprintf(p,sizeof p,"/sys/class/backlight/%s/brightness",e->d_name); f=open(p,O_WRONLY); if(f>=0){write(f,m,strlen(m));close(f);} } closedir(d);}
}

static void do_power(int cmd){
  sync(); mount(NULL,"/",NULL,MS_REMOUNT|MS_RDONLY,NULL); sync();
  reboot(cmd);
}

#ifndef HOST_TEST
#include <sys/wait.h>
#include <signal.h>
static void gui_main(int restarts,int lastsig){

  struct fbdev fb; memset(&fb,0,sizeof fb);
  for (int i=0;i<60 && fb_open(&fb);i++) sleep(1);
  if (!fb.ok){ kmsg("no /dev/fb0"); for(;;) sleep(60); }

  struct ui u; memset(&u,0,sizeof u);
  snprintf(u.l_sup,sizeof u.l_sup,"gui restarts: %d  last exit: %d",restarts,lastsig);
  u.W=fb.v.xres; u.H=fb.v.yres; u.S=u.W>=1000?3:(u.W>=600?2:1); u.pressed=B_NONE;
  u.px=calloc((size_t)u.W*u.H,4); layout(&u);
  snprintf(u.l_fb,sizeof u.l_fb,"fb0: %ux%u %ubpp stride %u  r%u g%u b%u",fb.v.xres,fb.v.yres,fb.v.bits_per_pixel,fb.f.line_length,fb.v.red.offset,fb.v.green.offset,fb.v.blue.offset);
  struct utsname un; if(!uname(&un)) snprintf(u.l_kern,sizeof u.l_kern,"kernel: %s %s",un.release,un.machine); else strcpy(u.l_kern,"kernel: ?");
  struct touch t; memset(&t,0,sizeof t); t.fd=-1;
  for (int i=0;i<30 && find_touch(&t);i++) sleep(1);
  if (t.fd>=0) snprintf(u.l_touch,sizeof u.l_touch,"touch: tap anywhere (x %d..%d, y %d..%d)",t.minx,t.maxx,t.miny,t.maxy);
  else strcpy(u.l_touch,"touch: no input device found");
  kmsg(u.l_fb); kmsg(u.l_touch);

  int rawx=0,rawy=0,have=0,was_down=0;
  for (;;){
    struct pollfd p={.fd=t.fd,.events=POLLIN};
    int changed=0, r=poll(&p,t.fd>=0?1:0,1000);
    if (r>0 && (p.revents&POLLIN)){
      struct input_event ev[64]; int n;
      while ((n=read(t.fd,ev,sizeof ev))>0){
        for (int i=0;i<n/(int)sizeof ev[0];i++){
          struct input_event*e=&ev[i];
          if (e->type==EV_ABS){
            if (e->code==(t.mt?ABS_MT_POSITION_X:ABS_X)) { rawx=e->value; have=1; }
            else if (e->code==(t.mt?ABS_MT_POSITION_Y:ABS_Y)) { rawy=e->value; have=1; }
            else if (t.mt && e->code==ABS_MT_TRACKING_ID) u.down=(e->value>=0);
          } else if (e->type==EV_KEY && e->code==BTN_TOUCH) u.down=e->value;
          else if (e->type==EV_SYN && e->code==SYN_REPORT) {
            if (have){
              u.rx=rawx; u.ry=rawy;
              u.tx=(int)((long long)(rawx-t.minx)*u.W/(t.maxx-t.minx+1));
              u.ty=(int)((long long)(rawy-t.miny)*u.H/(t.maxy-t.miny+1));
            }
            int hb=B_NONE; for(int b=0;b<B_COUNT;b++) if(hit(&u.btn[b],u.tx,u.ty)) hb=b;
            if (u.down){ u.pressed=hb; snprintf(u.l_touch,sizeof u.l_touch,"touch: x=%d y=%d (raw %d,%d)",u.tx,u.ty,u.rx,u.ry); }
            if (was_down && !u.down){
              if (u.pressed==B_TAP) u.taps++;
              else if (u.pressed==B_REBOOT){ do_power(RB_AUTOBOOT); }
              else if (u.pressed==B_OFF){ do_power(RB_POWER_OFF); }
              u.pressed=B_NONE;
            }
            was_down=u.down; changed=1;
          }
        }
      }
    }
    { FILE*f=fopen("/proc/uptime","r"); if(f){ double up=0; if(fscanf(f,"%lf",&up)==1) u.up=(long)up; fclose(f);} }
    (void)changed; if (g_crash) render_crash(&u); else render(&u); blit(&fb,&u);
  }
}

int main(int argc,char**argv){            /* PID 1: must never exit, or the kernel panics (PANIC_TIMEOUT=1 -> reboot loop) */
  if (argc>1 && !strcmp(argv[1],"--crash")) g_crash=1;
  mkdir("/proc",0755); mkdir("/sys",0755); mkdir("/dev",0755);
  if (access("/proc/self",F_OK)) mount("proc","/proc","proc",0,NULL);
  if (access("/sys/class",F_OK)) mount("sysfs","/sys","sysfs",0,NULL);
  if (access("/dev/null",F_OK)) mount("devtmpfs","/dev","devtmpfs",0,NULL);
  kmsg("started"); backlight();
  int restarts=0,lastsig=0;
  for (;;){
    pid_t c=fork();
    if (c==0){ gui_main(restarts,lastsig); _exit(1); }
    if (c<0){ sleep(2); continue; }
    for (;;){                                  /* also reaps orphans */
      int st; pid_t w=waitpid(-1,&st,0);
      if (w==c){ lastsig=WIFSIGNALED(st)?-WTERMSIG(st):WEXITSTATUS(st); break; }
      if (w<0 && errno!=EINTR) { sleep(1); break; }
    }
    restarts++;
    char m[96]; snprintf(m,sizeof m,"gui exited (%d), restart #%d",lastsig,restarts); kmsg(m);
    sleep(2);
  }
}
#else  /* host preview: ./gui_host out.ppm [taps] [pressed] */
int main(int argc,char**argv){
  struct ui u; memset(&u,0,sizeof u); u.W=800; u.H=1280; u.S=2; u.pressed=argc>3?atoi(argv[3]):B_NONE;
  u.px=calloc((size_t)u.W*u.H,4); layout(&u); u.taps=argc>2?atoi(argv[2]):0; u.up=42;
  strcpy(u.l_fb,"fb0: 800x1280 32bpp stride 3200  r16 g8 b0");
  strcpy(u.l_kern,"kernel: 4.19.127-g0e8bc5306 aarch64");
  strcpy(u.l_touch,"touch: x=400 y=700 (raw 400,700)"); u.down=1; u.tx=400; u.ty=700;
  if(getenv("MINE_CRASH")) render_crash(&u); else render(&u); FILE*f=fopen(argv[1],"wb"); fprintf(f,"P6\n%d %d\n255\n",u.W,u.H);
  for(size_t i=0;i<(size_t)u.W*u.H;i++){ unsigned char c[3]={u.px[i]>>16,u.px[i]>>8,u.px[i]}; fwrite(c,1,3,f);} fclose(f); return 0; }
#endif
