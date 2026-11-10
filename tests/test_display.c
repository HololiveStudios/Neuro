/* Teste do display em modo desktop (precisa de um X: use xvfb-run). */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L
#include "../display_xcb.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char**argv){
  int level=argc>1?atoi(argv[1]):1, jorge=argc>2?atoi(argv[2]):0, face=argc>3?atoi(argv[3]):2, i; double t0;
  PestinhaDisplay*d=display_open_desktop(jorge,level); int w,h,sw,sh; Area wa; Platform pl[8];
  if(!d){puts("sem display");return 1;}
  display_size(d,&w,&h); display_screen(d,&sw,&sh); display_work_area(d,&wa);
  printf("janela %dx%d tela %dx%d area %d,%d %dx%d\n",w,h,sw,sh,wa.x,wa.y,wa.w,wa.h);
  display_move(d,300,200);
  t0=now(); for(i=0;i<60;i++)display_draw(d,face,1,0,0,0,0,0,i*0.033);
  printf("display_draw: %.2f ms por quadro\n",(now()-t0)/60*1000);
  display_draw(d,face,1,0,0,0,0,0,1.0);
  printf("plataformas: %d\n",display_platforms(d,h+10,pl,8));
  usleep(300000);
  { char cmd[200]; snprintf(cmd,sizeof cmd,"import -window root -crop %dx%d+290+190 +repage /tmp/face_%d_%d_%d.png",w+20,h+20,level,jorge,face); (void)!system(cmd); }
  display_close(d); return 0;}
