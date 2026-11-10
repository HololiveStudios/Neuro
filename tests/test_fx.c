/* Teste visual dos efeitos (xvfb-run). Gera /tmp/fx_*.png */
#define _DEFAULT_SOURCE
#include "../fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){
  Fx*fx=fx_open(); Area work={0,0,1280,720}; int i, id; FxEvent ev;
  if(!fx){puts("sem X");return 1;}
  system("xsetroot -solid '#3b4252' 2>/dev/null");
  fx_poster(fx,40,40,"INJUSTIÇA!","Exijo mais carinho e menos trabalho agora mesmo",FX_STYLE_PROTEST,0);
  fx_poster(fx,300,60,"GREVE","Em greve até haver carinho.",FX_STYLE_PROTEST,0);
  id=fx_poster(fx,900,500,"CANTINHO DO CASTIGO","",FX_STYLE_SIGN,0);
  fx_poster(fx,560,120,"ninguém brinca comigo... tô com saudade do carinho",NULL,FX_STYLE_NOTE,0);
  fx_poster(fx,780,60,"FOI O PESTINHA","Eu estive aqui :)",FX_STYLE_PRANK,0);
  for(i=0;i<6;i++)fx_paper(fx,200+i*40,100,(i-3)*90,-100);
  fx_notebook(fx,1,500,500,120,90);
  fx_ball_enable(fx,1,700,200);
  fx_fireworks(fx,640,250,0); fx_fireworks(fx,300,300,1);
  for(i=0;i<45;i++){ fx_update(fx,1/30.0,&work,NULL,0); usleep(33000);}
  printf("posters=%d papers=%d particulas=%d bola=%d\n",fx_poster_count(fx),fx_paper_count(fx),fx_particle_count(fx),fx_ball_enabled(fx));
  system("import -window root /tmp/fx_scene.png");
  fx_remove(fx,id); fx_update(fx,1/30.0,&work,NULL,0);
  for(i=0;i<200;i++){ fx_update(fx,1/30.0,&work,NULL,0); usleep(5000);}
  printf("apos 200 quadros: particulas=%d (devem ter acabado)\n",fx_particle_count(fx));
  while(fx_poll(fx,&ev)) printf("evento %d\n",ev.type);
  fx_close(fx); return 0;}
