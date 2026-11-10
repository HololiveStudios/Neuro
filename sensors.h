#ifndef PESTINHA_SENSORS_H
#define PESTINHA_SENSORS_H

/* Subsistema de sentidos ambientais do Pestinha no Linux/X11:
   - Ritmo de digitação e movimento do mouse (sem capturar teclas ou títulos de janelas, preservando privacidade).
   - Classificação do aplicativo ativo por classe (trabalho vs navegação).
   - Novidade de aplicativos nunca vistos.
   - Tempo ocioso e reprodução de áudio via /proc/asound. */

typedef struct Sensors Sensors;

typedef struct {
    double work;           /* 0..1 aplicativo de trabalho/desenvolvimento */
    double web;            /* 0..1 navegador web */
    double novelty;        /* 0..1 pico ao encontrar um aplicativo nunca visto */
    double idle_seconds;   /* segundos desde o último movimento de mouse ou tecla */
    double motion;         /* 0..1 intensidade de movimento recente do mouse */
    double pointer_x;      /* coordenada X atual do cursor na tela */
    double pointer_y;      /* coordenada Y atual do cursor na tela */
    double typing;         /* 0..1 ritmo de digitação recente */
    double sound;          /* 0..1 se há som/música sendo reproduzido no sistema */
} SensorReading;

Sensors *sensors_open(void);
void     sensors_close(Sensors *s);
void     sensors_poll(Sensors *s, double now, SensorReading *out);

#endif
