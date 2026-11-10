/* Teste dos sensores contra um X virtual (xvfb-run). Gera eventos reais com XTEST. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "../sensors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
static xcb_extension_t xtest = {"XTEST", 0};
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static xcb_connection_t*c; static xcb_window_t root;
static void fake_key(int code,int down){
  static const xcb_protocol_request_t rq={4,&xtest,2,1}; struct iovec p[6]; unsigned char b[36]; memset(b,0,sizeof b);
  b[4]=down?2:3; b[5]=(unsigned char)code; p[2].iov_base=b;p[2].iov_len=36;p[3].iov_base=0;p[3].iov_len=0;p[4].iov_base=0;p[4].iov_len=0;p[5].iov_base=0;p[5].iov_len=0;
  xcb_send_request(c,0,p+2,&rq); xcb_flush(c);}
static xcb_window_t make_win(const char*inst,const char*klass){
  xcb_window_t w=xcb_generate_id(c); char buf[80]; int n=snprintf(buf,sizeof buf,"%s%c%s",inst,0,klass)+1;
  xcb_create_window(c,0,w,root,0,0,50,50,0,1,0,0,NULL);
  xcb_change_property(c,XCB_PROP_MODE_REPLACE,w,XCB_ATOM_WM_CLASS,XCB_ATOM_STRING,8,n,buf);
  xcb_map_window(c, w);
  xcb_flush(c); return w;}
static void set_active(xcb_window_t w){
  xcb_intern_atom_reply_t*r=xcb_intern_atom_reply(c,xcb_intern_atom(c,0,18,"_NET_ACTIVE_WINDOW"),0);
  xcb_change_property(c,XCB_PROP_MODE_REPLACE,root,r->atom,XCB_ATOM_WINDOW,32,1,&w); xcb_flush(c); free(r);}
static SensorReading run(Sensors*s,double secs,int mode){ /* mode 1: mouse, 2: teclas */
  SensorReading r={0}; double t0=now(),t; int i=0;
  while((t=now())-t0<secs){
    if(mode==1) xcb_warp_pointer(c,0,root,0,0,0,0,(i%2)?100:900,(i%2)?100:500);
    if(mode==2){ fake_key(38,1); fake_key(38,0); }
    xcb_flush(c); sensors_poll(s,t,&r); ++i; usleep(50000);}
  return r;}
static int fails=0;
#define CHECK(cond,msg) do{ if(cond)printf("ok: %s\n",msg); else{printf("FALHA: %s\n",msg);++fails;} }while(0)
int main(void){
  Sensors*s; SensorReading r; xcb_window_t ff,code; int sn=0;
  system("mkdir -p /tmp/fakeasound/card0/pcm0p/sub0 && printf 'closed\\n' > /tmp/fakeasound/card0/pcm0p/sub0/status");
  setenv("PESTINHA_ASOUND","/tmp/fakeasound",1);
  c=xcb_connect(NULL,&sn); root=xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
  s=sensors_open(); if(!s||!c){puts("sem X");return 1;}
  ff=make_win("Navigator","firefox"); code=make_win("code","Code"); set_active(code); r=run(s,1.0,0);
  printf("inicio: work=%.2f web=%.2f novelty=%.2f idle=%.1f\n",r.work,r.web,r.novelty,r.idle_seconds);
  set_active(ff); r=run(s,4.0,0);
  printf("firefox ativo (4s): web=%.2f work=%.2f novelty=%.2f\n",r.web,r.work,r.novelty);
  CHECK(r.web>0.7 && r.work<0.3,"app ativo Firefox vira o canal WEB (por classe, sem ler o titulo)");
  r=run(s,0.1,0); CHECK(r.idle_seconds>=0,"idle calculado");
  r=run(s,2.0,1); printf("mouse: motion=%.2f idle=%.2f ptr=%.0f,%.0f\n",r.motion,r.idle_seconds,r.pointer_x,r.pointer_y);
  CHECK(r.motion>0.5 && r.idle_seconds<0.3,"mover o mouse sobe MOTION e zera o ocioso");
  r=run(s,2.5,2); printf("teclas: typing=%.2f\n",r.typing);
  CHECK(r.typing>0.4,"apertar teclas sobe TYPING (so o ritmo)");
  r=run(s,6.0,0); printf("parado: typing=%.2f motion=%.2f idle=%.1f\n",r.typing,r.motion,r.idle_seconds);
  CHECK(r.typing<0.1 && r.motion<0.1 && r.idle_seconds>4,"parado: TYPING e MOTION voltam a zero e o ocioso cresce");
  system("printf 'state: RUNNING\\nowner_pid   : 42\\n' > /tmp/fakeasound/card0/pcm0p/sub0/status");
  r=run(s,4.0,0); printf("audio tocando: sound=%.2f\n",r.sound); CHECK(r.sound>0.7,"audio RUNNING sobe SOUND");
  set_active(code); r=run(s,5.0,0); printf("code ativo: work=%.2f web=%.2f\n",r.work,r.web);
  CHECK(r.work>0.7,"app ativo Code vira o canal WORK");
  { xcb_window_t nw=make_win("zzz","NovoApp"); set_active(nw); r=run(s,1.5,0); printf("app novo: novelty=%.2f\n",r.novelty); CHECK(r.novelty>0.5,"app nunca visto gera NOVELTY"); }
  sensors_close(s); printf(fails?"\n%d falha(s)\n":"\ntodos os testes passaram\n",fails); return fails!=0;}
