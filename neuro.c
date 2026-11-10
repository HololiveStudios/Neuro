#include "neuro.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_data.h"

#define NS (PESTINHA_CHANNELS * 6)
#define NL (PESTINHA_NEURONS - NS)
#define NPOOLS (PESTINHA_CHANNELS + PESTINHA_INTERNEURONS + PESTINHA_MOTORS)
#define MAX_SYNAPSE (PESTINHA_NEURONS * PESTINHA_NEURONS)
#define PLASTIC_POSTS 4
#define LEARN_MIN (-0.05)
#define LEARN_MAX 0.08
#define REFRACTORY 3
#define NOISE_SIGMA 0.025

struct Brain {
    unsigned int rng;
    int starts[NPOOLS], sizes[NPOOLS], kind[NPOOLS];
    const char *names[NPOOLS];
    double w[MAX_SYNAPSE], base[MAX_SYNAPSE];
    double v[NL], adapt[NL], adapt_b[NL], adapt_decay[NL];
    double decay[NL]; int refractory[NL];
    double spike[PESTINHA_NEURONS], pre_gain[PESTINHA_NEURONS];
    double sensory_rate[NS], sensory_d[NS], sensory_rec[NS], efficacy[NS], probability[NS];
    double levels[PESTINHA_CHANNELS], eligibility[PESTINHA_CHANNELS];
    double learned[PESTINHA_CHANNELS][PLASTIC_POSTS];
    double plastic_mask[PESTINHA_CHANNELS][PLASTIC_POSTS][72];
    double pool_counts[NPOOLS], pool_rates[NPOOLS];
    double time_ms;
};

static const char *plastic_targets[PLASTIC_POSTS]={"PLAYFUL","CALM","FRUSTRATION","GLOOM"};
static const double plastic_sign[PLASTIC_POSTS]={1.0,1.0,-1.0,-1.0};
static unsigned int rand_u(unsigned int*state){unsigned int x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x?x:0x9e3779b9u;return *state;}
static double urand(Brain*b){return ((rand_u(&b->rng)>>8)+1.0)/16777217.0;}
static double uniform(unsigned int*state){return ((rand_u(state)>>8)+1.0)/16777217.0;}
static double normal(Brain*b){double u=urand(b),v=urand(b);return sqrt(-2.0*log(u))*cos(6.283185307179586*v);}
static double normal_state(unsigned int*state){double u=uniform(state),v=uniform(state);return sqrt(-2.0*log(u))*cos(6.283185307179586*v);}
int brain_channel_index(const char*n){int i;for(i=0;i<PESTINHA_CHANNELS;i++)if(!strcmp(n,SENS[i].name))return i;return -1;}
int brain_motor_index(const char*n){int i;for(i=0;i<PESTINHA_MOTORS;i++)if(!strcmp(n,MOTOR[i].name))return i;return -1;}
static int find_pool(const Brain*b,const char*n){int i;for(i=0;i<NPOOLS;i++)if(!strcmp(n,b->names[i]))return i;return -1;}
const char*brain_channel_name(int i){return i>=0&&i<PESTINHA_CHANNELS?SENS[i].name:"";}
const char*brain_motor_name(int i){return i>=0&&i<PESTINHA_MOTORS?MOTOR[i].name:"";}
const char*brain_interneuron_name(int i){return i>=0&&i<PESTINHA_INTERNEURONS?INTER[i].name:"";}
static double temperament_gain(const char*temperament,const char*pre,const char*post){
    static const char *jorge[][3]={{"BOREDOM","CURIOUS","1.5"},{"BOREDOM","PLAYFUL","1.4"},{"LONELY","GLOOM",".6"},{"POKE","PLAYFUL","1.6"},{"CARESS","PLAYFUL","1.7"},{"CARESS","CALM","1.2"},{"DISMISS","FRUSTRATION","1.6"},{"RECUSA","FRUSTRATION","1.3"},{"TOY_TAKEN","GLOOM",".5"},{"TOY_TAKEN","FRUSTRATION","1.3"},{"NOVELTY","CURIOUS","1.3"}};
    static const char *bryan[][3]={{"*","CURIOUS","1.3"},{"*","PLAYFUL","1.1"},{"DRAG","FRUSTRATION",".5"},{"TOY_TAKEN","GLOOM","1.5"},{"TOY_TAKEN","FRUSTRATION",".6"},{"TOY_NEAR","PLAYFUL","1.3"},{"DISMISS","FRUSTRATION","1.3"},{"IMPACT","FRUSTRATION","1.2"}};
    const char *(*table)[3]=NULL;int count=0,i;if(!temperament)return 1.0;
    if(!strcmp(temperament,"jorge")){table=jorge;count=(int)(sizeof(jorge)/sizeof(jorge[0]));}
    else if(!strcmp(temperament,"bryan")){table=bryan;count=(int)(sizeof(bryan)/sizeof(bryan[0]));}
    for(i=0;i<count;i++)if(!strcmp(table[i][1],post)&&(!strcmp(table[i][0],pre)||!strcmp(table[i][0],"*")))return atof(table[i][2]);
    return 1.0;
}
static void connect_pools(Brain*b,const char*pre,const char*post,double weight,double density){int a=find_pool(b,pre),z=find_pool(b,post),i,j;double mean=exp(.25*.25/2.0);if(a<0||z<0)return;for(i=0;i<b->sizes[a];i++)for(j=0;j<b->sizes[z];j++)if(urand(b)<density){int from=b->starts[a]+i,to=b->starts[z]+j;b->w[from*PESTINHA_NEURONS+to]+=weight*(exp(.25*normal(b))/mean);}}
Brain*brain_create(unsigned int seed,const char*temperament){Brain*b=calloc(1,sizeof(*b));unsigned int plastic_rng_state;int i,j,cursor=0;double mean=exp(.25*.25/2.0);if(!b)return NULL;b->rng=seed?seed:7u;plastic_rng_state=(seed?seed:7u)+1u;
    for(i=0;i<PESTINHA_CHANNELS;i++){int n=SENS[i].n;b->starts[i]=cursor;b->sizes[i]=n;b->kind[i]=0;b->names[i]=SENS[i].name;cursor+=n;for(j=0;j<n;j++){b->sensory_rate[b->starts[i]+j]=SENS[i].rate;b->sensory_d[b->starts[i]+j]=SENS[i].d;b->sensory_rec[b->starts[i]+j]=.001/SENS[i].tau_rec;b->efficacy[b->starts[i]+j]=1.0;}}
    for(i=0;i<PESTINHA_INTERNEURONS;i++){int p=PESTINHA_CHANNELS+i;b->starts[p]=cursor;b->sizes[p]=INTER[i].n;b->kind[p]=!strcmp(INTER[i].kind,"inh")?2:1;b->names[p]=INTER[i].name;cursor+=b->sizes[p];for(j=0;j<b->sizes[p];j++){int q=b->starts[p]+j-NS;b->adapt_b[q]=INTER[i].b;b->adapt_decay[q]=exp(-.001/INTER[i].bt);b->decay[q]=exp(-1.0/INTER[i].tau);}}
    for(i=0;i<PESTINHA_MOTORS;i++){int p=PESTINHA_CHANNELS+PESTINHA_INTERNEURONS+i;b->starts[p]=cursor;b->sizes[p]=MOTOR[i].n;b->kind[p]=3;b->names[p]=MOTOR[i].name;cursor+=b->sizes[p];for(j=0;j<b->sizes[p];j++){int q=b->starts[p]+j-NS;b->adapt_b[q]=MOTOR[i].b;b->adapt_decay[q]=exp(-.001/MOTOR[i].bt);b->decay[q]=exp(-1.0/MOTOR[i].tau);}}
    if(cursor!=PESTINHA_NEURONS){free(b);return NULL;}for(i=0;i<PESTINHA_NEURONS;i++)b->pre_gain[i]=1.0;
    for(i=0;i<(int)(sizeof(CONNS)/sizeof(CONNS[0]));i++){double w=CONNS[i].weight;int ch=brain_channel_index(CONNS[i].pre);if(ch>=0)w*=temperament_gain(temperament,CONNS[i].pre,CONNS[i].post);connect_pools(b,CONNS[i].pre,CONNS[i].post,w,CONNS[i].density);}
    for(i=0;i<PESTINHA_MOTORS;i++)for(j=0;j<PESTINHA_MOTORS;j++)if(i!=j){int di=(i>=8),dj=(j>=8);if(di==dj)connect_pools(b,MOTOR[i].name,MOTOR[j].name,!strcmp(MOTOR[i].name,"STARTLE")?-.10:-.05,.7);}
    memcpy(b->base,b->w,sizeof(b->w));
    for(i=0;i<PESTINHA_CHANNELS;i++)for(j=0;j<PLASTIC_POSTS;j++){int k;for(k=0;k<72;k++)b->plastic_mask[i][j][k]=(uniform(&plastic_rng_state)<.5)?exp(.25*normal_state(&plastic_rng_state))/mean:0.0;}
    return b;}
void brain_destroy(Brain*b){free(b);}
void brain_stimulate(Brain*b,const double values[PESTINHA_CHANNELS]){int c,j;if(!b)return;memset(b->probability,0,sizeof(b->probability));for(c=0;c<PESTINHA_CHANNELS;c++){double x=fmax(0,fmin(1,values[c]));b->levels[c]=x;for(j=0;j<b->sizes[c];j++){int n=b->starts[c]+j;b->probability[n]=b->sensory_rate[n]*x*.001;}}}
static void substep(Brain*b){int i,j;double drive[NL];memset(drive,0,sizeof(drive));for(i=0;i<PESTINHA_NEURONS;i++)if(b->spike[i]!=0){const double*row=&b->w[i*PESTINHA_NEURONS+NS];double gain=b->pre_gain[i]*b->spike[i];for(j=0;j<NL;j++)drive[j]+=gain*row[j];}
    for(i=0;i<NL;i++){b->v[i]=b->v[i]*b->decay[i]+drive[i]+NOISE_SIGMA*normal(b);if(b->refractory[i]>0){b->v[i]=0;b->refractory[i]--;}if(b->v[i]<-.5)b->v[i]=-.5;b->adapt[i]*=b->adapt_decay[i];if(b->v[i]>=1+b->adapt[i]){b->v[i]=0;b->refractory[i]=REFRACTORY;b->spike[NS+i]=1;b->adapt[i]+=b->adapt_b[i];}else b->spike[NS+i]=0;}
    for(i=0;i<NS;i++){b->spike[i]=(urand(b)<b->probability[i])?1:0;if(b->spike[i])b->efficacy[i]*=1-b->sensory_d[i];b->efficacy[i]+=(1-b->efficacy[i])*b->sensory_rec[i];b->pre_gain[i]=b->efficacy[i];}
    for(i=0;i<NPOOLS;i++){int k;for(k=0;k<b->sizes[i];k++)b->pool_counts[i]+=b->spike[b->starts[i]+k];}}
void brain_step(Brain*b,double milliseconds){int n,i;if(!b)return;n=(int)fmax(1,fmin(400,round(milliseconds)));for(i=0;i<n;i++)substep(b);for(i=0;i<NPOOLS;i++){double r=b->pool_counts[i]/(n*.001*b->sizes[i]);double alpha=1-exp(-(n*.001)/.5);b->pool_rates[i]+=alpha*(r-b->pool_rates[i]);b->pool_counts[i]=0;}b->time_ms+=n;}
double brain_motor(const Brain*b,int i){return b&&i>=0&&i<PESTINHA_MOTORS?b->pool_rates[PESTINHA_CHANNELS+PESTINHA_INTERNEURONS+i]:0;}
double brain_interneuron(const Brain*b,int i){return b&&i>=0&&i<PESTINHA_INTERNEURONS?b->pool_rates[PESTINHA_CHANNELS+i]:0;}
double brain_efficacy(const Brain*b,int c){int j;double sum=0;if(!b||c<0||c>=PESTINHA_CHANNELS)return 0;for(j=0;j<b->sizes[c];j++)sum+=b->efficacy[b->starts[c]+j];return sum/b->sizes[c];}
void brain_learn(Brain*b,double reward,double seconds,double rate){int i,j;if(!b)return;double k=1-exp(-seconds/4.0);for(i=0;i<PESTINHA_CHANNELS;i++)b->eligibility[i]+=(b->levels[i]-b->eligibility[i])*k;if(reward==0)return;for(i=0;i<PESTINHA_CHANNELS;i++)for(j=0;j<PLASTIC_POSTS;j++)b->learned[i][j]=fmax(LEARN_MIN,fmin(LEARN_MAX,b->learned[i][j]+rate*reward*seconds*b->eligibility[i]*plastic_sign[j]));memcpy(b->w,b->base,sizeof(b->w));for(i=0;i<PESTINHA_CHANNELS;i++){int pre=b->starts[i];for(j=0;j<PLASTIC_POSTS;j++){int post=find_pool(b,plastic_targets[j]),x,y;if(post<0)continue;for(x=0;x<b->sizes[i];x++)for(y=0;y<b->sizes[post];y++)b->w[(pre+x)*PESTINHA_NEURONS+b->starts[post]+y]+=b->learned[i][j]*b->plastic_mask[i][j][x*b->sizes[post]+y];}}}
void brain_reset_habituation(Brain*b){int i;if(b)for(i=0;i<NS;i++)b->efficacy[i]=b->pre_gain[i]=1.0;}
static void apply_learning(Brain*b){int i,j;memcpy(b->w,b->base,sizeof(b->w));for(i=0;i<PESTINHA_CHANNELS;i++){int pre=b->starts[i];for(j=0;j<PLASTIC_POSTS;j++){int post=find_pool(b,plastic_targets[j]),x,y;if(post<0||b->learned[i][j]==0)continue;for(x=0;x<b->sizes[i];x++)for(y=0;y<b->sizes[post];y++)b->w[(pre+x)*PESTINHA_NEURONS+b->starts[post]+y]+=b->learned[i][j]*b->plastic_mask[i][j][x*b->sizes[post]+y];}}}
void brain_save(const Brain*b,const char*path){FILE*f;int i,j;if(!b||!path||(f=fopen(path,"w"))==NULL)return;fprintf(f,"PESTINHA_BRAIN 1 %u\n",b->rng);for(i=0;i<NS;i++)fprintf(f,"%.9g%c",b->efficacy[i],(i+1)%12?' ':'\n');for(i=0;i<PESTINHA_CHANNELS;i++)for(j=0;j<PLASTIC_POSTS;j++)fprintf(f,"%.9g%c",b->learned[i][j],j+1==PLASTIC_POSTS?'\n':' ');fclose(f);}
int brain_load(Brain*b,const char*path){FILE*f;char tag[32];int ver,i,j;unsigned int rng;double eff[NS],learn[PESTINHA_CHANNELS][PLASTIC_POSTS];if(!b||!path||(f=fopen(path,"r"))==NULL)return 0;if(fscanf(f,"%31s %d %u",tag,&ver,&rng)!=3||strcmp(tag,"PESTINHA_BRAIN")||ver!=1){fclose(f);return 0;}for(i=0;i<NS;i++)if(fscanf(f,"%lf",&eff[i])!=1||eff[i]<0||eff[i]>1){fclose(f);return 0;}for(i=0;i<PESTINHA_CHANNELS;i++)for(j=0;j<PLASTIC_POSTS;j++)if(fscanf(f,"%lf",&learn[i][j])!=1||learn[i][j]<LEARN_MIN||learn[i][j]>LEARN_MAX){fclose(f);return 0;}fclose(f);memcpy(b->efficacy,eff,sizeof(eff));memcpy(b->learned,learn,sizeof(learn));b->rng=rng;for(i=0;i<NS;i++)b->pre_gain[i]=b->efficacy[i];apply_learning(b);return 1;}
