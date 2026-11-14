/* init_launcher.c - PID 1 of "My OS". Installed as /sbin/init inside the system partition.
 *
 *   mounts /proc /sys /dev /tmp -> loads /etc/mine/env -> waits for the screen
 *   -> runs the HOME screen (python) -> home writes /run/launch -> runs that APP
 *   -> app exits (Back) -> home again.   If something CRASHES, /sbin/fallback-gui shows the
 *   captured output on screen for a few seconds, then everything restarts.
 *
 * PID 1 never exits (the kernel would panic and, on this kernel, reboot after 1 s).
 * Test hooks (MINE_TEST=1, MINE_HOME_CMD, MINE_FALLBACK, MINE_RETRY, MINE_LOG, MINE_LAUNCH)
 * exist so this can be tried on a PC; they change nothing on the device.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define BITS_PER_LONG (sizeof(long)*8)
#define TESTBIT(a,b) (((a)[(b)/BITS_PER_LONG] >> ((b)%BITS_PER_LONG)) & 1)

static const char *LOG;

static void kmsg(const char *s){ int f=open("/dev/kmsg",O_WRONLY); if(f>=0){ dprintf(f,"<6>launcher: %s\n",s); close(f);} }
static void logf_(const char *fmt, ...) __attribute__((format(printf,1,2)));
static void logf_(const char *fmt, ...){
  char b[512]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
  int fd=open(LOG,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){ dprintf(fd,"%s\n",b); close(fd); }
  kmsg(b);
}

static void load_env(const char *path){
  FILE *f=fopen(path,"r"); if(!f) return; char l[1024];
  while(fgets(l,sizeof l,f)){
    char *e=strpbrk(l,"\r\n"); if(e) *e=0;
    if(!l[0]||l[0]=='#') continue;
    char *eq=strchr(l,'='); if(!eq) continue; *eq=0; setenv(l,eq+1,1);
  }
  fclose(f);
}

static void backlight(void){            /* best effort: make sure the panel is lit */
  const char *led="/sys/class/leds/lcd-backlight/";
  char p[300], mx[16]="255"; int f;
  snprintf(p,sizeof p,"%smax_brightness",led); f=open(p,O_RDONLY); if(f>=0){ int n=read(f,mx,sizeof mx-1); if(n>0) mx[n]=0; close(f); }
  snprintf(p,sizeof p,"%sbrightness",led); f=open(p,O_WRONLY); if(f>=0){ if(write(f,mx,strlen(mx))<0){} close(f); }
  DIR *d=opendir("/sys/class/backlight");
  if(d){ struct dirent *e; while((e=readdir(d))){ if(e->d_name[0]=='.') continue; char m[16]="255";
      snprintf(p,sizeof p,"/sys/class/backlight/%s/max_brightness",e->d_name); f=open(p,O_RDONLY); if(f>=0){ int n=read(f,m,sizeof m-1); if(n>0) m[n]=0; close(f); }
      snprintf(p,sizeof p,"/sys/class/backlight/%s/brightness",e->d_name); f=open(p,O_WRONLY); if(f>=0){ if(write(f,m,strlen(m))<0){} close(f); } }
    closedir(d); }
}

/* Qt's linuxfb backend can only find touchscreens through udev's database, which does not
 * exist here, so we find the device ourselves and hand Qt its path. */
static int find_touch(char *out, size_t n){
  for(int i=0;i<32;i++){
    char path[64]; snprintf(path,sizeof path,"/dev/input/event%d",i);
    int fd=open(path,O_RDONLY|O_NONBLOCK); if(fd<0) continue;
    unsigned long abs_[(ABS_MAX+BITS_PER_LONG)/BITS_PER_LONG]; memset(abs_,0,sizeof abs_);
    int ok = ioctl(fd,EVIOCGBIT(EV_ABS,sizeof abs_),abs_)>=0 &&
             ((TESTBIT(abs_,ABS_MT_POSITION_X)&&TESTBIT(abs_,ABS_MT_POSITION_Y)) || (TESTBIT(abs_,ABS_X)&&TESTBIT(abs_,ABS_Y)));
    close(fd);
    if(ok){ snprintf(out,n,"%s",path); return 0; }
  }
  return -1;
}

static pid_t spawn(const char *cmd, int to_log, const char *extra){
  char *dup=strdup(cmd); char *argv[40]; int n=0;
  for(char *t=strtok(dup," \t"); t && n<36; t=strtok(NULL," \t")) argv[n++]=t;
  if(extra) argv[n++]=(char*)extra;
  argv[n]=NULL;
  pid_t p=fork();
  if(p==0){
    if(to_log){ int fd=open(LOG,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){ dup2(fd,1); dup2(fd,2); close(fd); } }
    int nul=open("/dev/null",O_RDONLY); if(nul>=0){ dup2(nul,0); close(nul); }
    execv(argv[0],argv);
    fprintf(stderr,"[launcher] cannot exec %s: %s\n",argv[0],strerror(errno));
    _exit(127);
  }
  free(dup); return p;
}

static pid_t pump_pid=0; static const char *pump_cmd=NULL;
static void start_pump(void){ pump_pid=spawn(pump_cmd,0,NULL); }

/* splash: shown while Python/Qt start; it exits by itself when the UI creates ui.ready */
static pid_t splash_pid=0; static const char *splash_cmd=NULL; static int splash_on=1; static char ready_path[300];
static void splash_start(const char *label){
  if(!splash_on || access(splash_cmd,X_OK)) return;
  unlink(ready_path);
  splash_pid=spawn(splash_cmd,0,label);
}
static void splash_stop(void){
  if(splash_pid>0){ kill(splash_pid,SIGKILL); waitpid(splash_pid,NULL,0); splash_pid=0; }
}
static void make_label(const char *cmd,const char *home,int first,char *out,size_t n){
  if(!strcmp(cmd,home)){ snprintf(out,n,"%s",first?"starting":"loading"); return; }
  char t[1100]; const char *tok=strrchr(cmd,' '); snprintf(t,sizeof t,"%s",tok?tok+1:cmd);
  char *sl=strrchr(t,'/'); const char *name=t;
  if(sl){ *sl=0; sl=strrchr(t,'/'); name=sl?sl+1:t; }
  snprintf(out,n,"opening %s",name);
}

static int wait_for(pid_t pid){         /* also reaps orphans, as PID 1 must; keeps fb-pump alive */
  int st;
  for(;;){
    pid_t w=waitpid(-1,&st,0);
    if(w==pid) return st;
    if(w>0 && w==pump_pid){ logf_("[launcher] fb-pump exited, restarting"); sleep(1); start_pump(); continue; }
    if(w<0 && errno!=EINTR) return -1;
  }
}

int main(void){
  int test=getenv("MINE_TEST")!=NULL;
  if(!test){
    mkdir("/proc",0755); mkdir("/sys",0755); mkdir("/dev",0755); mkdir("/tmp",01777);
    if(access("/proc/self",F_OK)) mount("proc","/proc","proc",0,NULL);
    if(access("/sys/class",F_OK)) mount("sysfs","/sys","sysfs",0,NULL);
    if(access("/dev/null",F_OK))  mount("devtmpfs","/dev","devtmpfs",0,NULL);
    mount("tmpfs","/tmp","tmpfs",0,"mode=1777");            /* rootfs is read-only; /tmp is RAM */
    mkdir("/tmp/xdg",0700);
    backlight();
  }
  setenv("PATH","/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",0);
  load_env(getenv("MINE_ENV")?getenv("MINE_ENV"):"/etc/mine/env");

  LOG=getenv("MINE_LOG")?getenv("MINE_LOG"):"/run/app.log";
  const char *launch=getenv("MINE_LAUNCH")?getenv("MINE_LAUNCH"):"/run/launch";
  const char *home=getenv("MINE_HOME_CMD")?getenv("MINE_HOME_CMD"):"/usr/bin/python3 -u /opt/mine/home/home.py";
  const char *fb=getenv("MINE_FALLBACK")?getenv("MINE_FALLBACK"):"/sbin/fallback-gui";
  int retry=getenv("MINE_RETRY")?atoi(getenv("MINE_RETRY")):10;
  kmsg("started");
  const char *rd=getenv("MINE_RUN")?getenv("MINE_RUN"):"/run";
  snprintf(ready_path,sizeof ready_path,"%s",getenv("MINE_UIREADY")?getenv("MINE_UIREADY"):"");
  if(!ready_path[0]) snprintf(ready_path,sizeof ready_path,"%s/ui.ready",rd);
  setenv("MINE_UIREADY",ready_path,1);                    /* splash and UI must agree on this path */
  splash_cmd=getenv("MINE_SPLASH_CMD")?getenv("MINE_SPLASH_CMD"):"/sbin/splash";
  splash_on=(getenv("MINE_SPLASH")?atoi(getenv("MINE_SPLASH")):1) && (!test || getenv("MINE_SPLASH_CMD"));

  pump_cmd=getenv("MINE_PUMP")?getenv("MINE_PUMP"):"/sbin/fb-pump";
  const char *rundir=getenv("MINE_RUN")?getenv("MINE_RUN"):"/run";
  if(!test){
    for(int i=0;i<30 && access("/dev/fb0",F_OK);i++) sleep(1);
    char tp[64]; if(!getenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS")){
      for(int i=0;i<10 && find_touch(tp,sizeof tp);i++) sleep(1);
      if(!find_touch(tp,sizeof tp)){ setenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS",tp,1); kmsg(tp); }
      else kmsg("no touchscreen found");
    }
  }

  if(!test || getenv("MINE_PUMP")){          /* display refresh + one-time display reset, BEFORE any UI */
    char rp[300]; snprintf(rp,sizeof rp,"%s/fbpump.ready",rundir); unlink(rp);
    start_pump();
    for(int i=0;i<50 && access(rp,F_OK);i++) usleep(100000);          /* wait up to 5 s */
    kmsg(access(rp,F_OK)?"fb-pump not ready (continuing)":"fb-pump ready");
  }

  char cmd[1024]; snprintf(cmd,sizeof cmd,"%s",home);
  for(int run=1;;run++){
    int lf=open(LOG,O_WRONLY|O_CREAT|O_TRUNC,0644); if(lf>=0) close(lf);
    logf_("[launcher] run %d: %s",run,cmd);
    time_t t0=time(NULL);
    char label[1200]; make_label(cmd,home,run==1,label,sizeof label);
    splash_start(label);
    pid_t a=spawn(cmd,1,NULL);
    int st = a>0 ? wait_for(a) : -1;
    splash_stop();
    long dt=(long)(time(NULL)-t0);
    int ok = a>0 && WIFEXITED(st) && WEXITSTATUS(st)==0;
    if(ok){
      char req[1024]={0}; FILE *f=fopen(launch,"r");
      if(f){ if(fgets(req,sizeof req,f)){ char *e=strpbrk(req,"\r\n"); if(e)*e=0; } fclose(f); unlink(launch); }
      if(req[0]){ snprintf(cmd,sizeof cmd,"%s",req); continue; }         /* home asked for an app */
      snprintf(cmd,sizeof cmd,"%s",home);                                 /* app said Back -> home */
      if(dt<2) sleep(2);                                                  /* no busy loop if home exits instantly */
      continue;
    }
    if(a<0)                     logf_("[launcher] fork failed");
    else if(WIFSIGNALED(st))    logf_("[launcher] app killed by signal %d after %lds",WTERMSIG(st),dt);
    else                        logf_("[launcher] app exited with code %d after %lds",WEXITSTATUS(st),dt);
    pid_t f=spawn(fb,0,"--crash");
    sleep(retry);
    if(f>0){ kill(f,SIGKILL); waitpid(f,NULL,0); }
    while(waitpid(-1,NULL,WNOHANG)>0) {}
    snprintf(cmd,sizeof cmd,"%s",home);
  }
}
