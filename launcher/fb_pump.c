/* fb_pump.c - keeps the MediaTek display refreshing. Installed as /sbin/fb-pump, started by the launcher.
 *
 * Why: mtkfb only pushes the framebuffer to the panel when the driver is told to (pan/trigger).
 * Qt's linuxfb backend just writes pixels into /dev/fb0 memory, so after a few idle seconds the
 * display pipeline goes idle and the picture freezes. Panning in a loop keeps it live.
 * It also does a one-time blank/unblank, which resets the display pipeline and normally drops
 * stale overlay layers left by the bootloader (the orange "device is unlocked" box).
 *
 * Control files (all in $MINE_RUN, default /run):
 *   fbpump.ready    appears when startup (incl. the reset) is done
 *   fbpump.info     framebuffer geometry
 *   fbpump.status   live counters
 *   fbpump.cmd      write one line:  reset | unblank | mode pan|put|both|none | hz N
 * Env: MINE_PUMP_HZ (30)  MINE_PUMP_MODE (pan)  MINE_FB_RESET (1)  MINE_FB (/dev/fb0)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static const char *RUN="/run";
enum { M_NONE, M_PAN, M_PUT, M_BOTH };
static const char *MN[]={"none","pan","put","both"};

static void put_file(const char *name, const char *text){
  char p[300], t[310]; snprintf(p,sizeof p,"%s/%s",RUN,name); snprintf(t,sizeof t,"%s.tmp",p);
  int f=open(t,O_WRONLY|O_CREAT|O_TRUNC,0644); if(f<0) return;
  if(write(f,text,strlen(text))<0){} close(f); rename(t,p);
}
static void backlight(void){
  char p[300], m[16]="255"; int f;
  f=open("/sys/class/leds/lcd-backlight/max_brightness",O_RDONLY); if(f>=0){ int n=read(f,m,sizeof m-1); if(n>0)m[n]=0; close(f); }
  f=open("/sys/class/leds/lcd-backlight/brightness",O_WRONLY); if(f>=0){ if(write(f,m,strlen(m))<0){} close(f); }
  DIR *d=opendir("/sys/class/backlight");
  if(d){ struct dirent *e; while((e=readdir(d))){ if(e->d_name[0]=='.') continue; char mm[16]="255";
    snprintf(p,sizeof p,"/sys/class/backlight/%s/max_brightness",e->d_name); f=open(p,O_RDONLY); if(f>=0){ int n=read(f,mm,sizeof mm-1); if(n>0)mm[n]=0; close(f); }
    snprintf(p,sizeof p,"/sys/class/backlight/%s/brightness",e->d_name); f=open(p,O_WRONLY); if(f>=0){ if(write(f,mm,strlen(mm))<0){} close(f); } } closedir(d); }
}
static int read_cmd(char *buf,size_t n){
  char p[300]; snprintf(p,sizeof p,"%s/fbpump.cmd",RUN);
  int f=open(p,O_RDONLY); if(f<0) return 0;
  int r=read(f,buf,n-1); close(f); unlink(p);
  if(r<=0) return 0;
  buf[r]=0;
  char *e=strpbrk(buf,"\r\n"); if(e) *e=0;
  return 1;
}

int main(void){
  if(getenv("MINE_RUN")) RUN=getenv("MINE_RUN");
  const char *dev=getenv("MINE_FB")?getenv("MINE_FB"):"/dev/fb0";
  int hz=getenv("MINE_PUMP_HZ")?atoi(getenv("MINE_PUMP_HZ")):30; if(hz<1) hz=1; if(hz>120) hz=120;
  int mode=M_PAN; if(getenv("MINE_PUMP_MODE")) for(int i=0;i<4;i++) if(!strcmp(getenv("MINE_PUMP_MODE"),MN[i])) mode=i;
  int do_reset=getenv("MINE_FB_RESET")?atoi(getenv("MINE_FB_RESET")):1;

  int fd; while((fd=open(dev,O_RDWR))<0) sleep(1);
  struct fb_var_screeninfo v; struct fb_fix_screeninfo f; memset(&v,0,sizeof v); memset(&f,0,sizeof f);
  if(getenv("MINE_FB_FAKE")){ v.xres=v.xres_virtual=800; v.yres=1280; v.yres_virtual=2560; v.bits_per_pixel=32; f.line_length=3200; f.smem_len=3200*2560; strcpy(f.id,"fake"); }  /* PC test hook */
  else while(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f)) sleep(1);
  char b[512];
  snprintf(b,sizeof b,"name=%.16s\nres=%ux%u\nvirt=%ux%u\nbpp=%u\nstride=%u\nsmem=%u\nyoffset=%u\n",
           f.id,v.xres,v.yres,v.xres_virtual,v.yres_virtual,v.bits_per_pixel,f.line_length,f.smem_len,v.yoffset);
  put_file("fbpump.info",b);

  long pans=0,puts_=0,errs=0,resets=0; int last_errno=0, consec=0; int fell_back=0;
  #define RESET() do{ ioctl(fd,FBIOBLANK,FB_BLANK_POWERDOWN); usleep(300000); ioctl(fd,FBIOBLANK,FB_BLANK_UNBLANK); usleep(200000); backlight(); resets++; }while(0)
  ioctl(fd,FBIOBLANK,FB_BLANK_UNBLANK); backlight();
  if(do_reset) RESET();
  put_file("fbpump.ready","1\n");

  time_t last=0;
  for(;;){
    char c[128];
    while(read_cmd(c,sizeof c)){
      if(!strcmp(c,"reset")) RESET();
      else if(!strcmp(c,"unblank")){ ioctl(fd,FBIOBLANK,FB_BLANK_UNBLANK); backlight(); }
      else if(!strncmp(c,"mode ",5)){
        for(int i=0;i<4;i++){ if(!strcmp(c+5,MN[i])){ mode=i; consec=0; fell_back=0; } }
      }
      else if(!strncmp(c,"hz ",3)){ hz=atoi(c+3); if(hz<1)hz=1; if(hz>120)hz=120; }
    }
    if(mode==M_PAN||mode==M_BOTH){
      struct fb_var_screeninfo p=v; p.xoffset=0; p.yoffset=0; p.activate=FB_ACTIVATE_NOW;
      if(ioctl(fd,FBIOPAN_DISPLAY,&p)<0){ errs++; last_errno=errno; consec++; } else { pans++; consec=0; }
      if(mode==M_PAN && consec>=20 && !fell_back){ mode=M_PUT; fell_back=1; consec=0; }   /* pan unsupported -> try put */
    }
    if(mode==M_PUT||mode==M_BOTH){
      struct fb_var_screeninfo p=v; p.yoffset=0; p.activate=FB_ACTIVATE_NOW|FB_ACTIVATE_FORCE;
      if(ioctl(fd,FBIOPUT_VSCREENINFO,&p)<0){ errs++; last_errno=errno; } else puts_++;
    }
    time_t now=time(NULL);
    if(now!=last){ last=now;
      snprintf(b,sizeof b,"mode=%s\nhz=%d\npans=%ld\nputs=%ld\nerrors=%ld\nlast_errno=%d\nresets=%ld\nauto_fallback=%d\n",MN[mode],hz,pans,puts_,errs,last_errno,resets,fell_back);
      put_file("fbpump.status",b); }
    usleep(1000000/hz);
  }
}
