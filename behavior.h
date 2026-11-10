#ifndef PESTINHA_BEHAVIOR_H
#define PESTINHA_BEHAVIOR_H

#include "conditioning.h"
#include "neuro.h"
#include "runtime_types.h"

/* Limiares em Hz/segundos. Ajuste aqui; os valores do original ficam em
   pestinha/ajustes.py e mind/castigo_ctl.py (LIMIAR_PERDAO_HZ). */
#define ACCEPT_FORGIVE_HZ    12.0 /* ACEITAR: perdoa o castigo (nao conferido com o original) */
#define CANTO_ENTER_HZ        3.0 /* CANTO >= 3 Hz: vai para o cantinho de tristeza (doc) */
#define CANTO_LEAVE_HZ        1.5 /* histerese para sair do cantinho de tristeza */
#define CARTAZ_PROTEST_HZ     3.0 /* CARTAZ minimo para protestar no castigo */
#define CARTAZ_PROTEST_AFTER  8.0 /* segundos no castigo antes de protestar */
#define PROTEST_REST_SECONDS 60.0 /* descanso depois que um protesto acaba */

void face_force(FaceState *face, double now, int index, double seconds);
void behavior_update_emotion(Emotion *emotion, Behavior *behavior,
                             FaceState *face, const Brain *brain,
                             double now, double dt);
void behavior_update(Behavior *behavior, Drives *drives,
                     ConditioningState *conditioning, const Brain *brain,
                     double inputs[PESTINHA_CHANNELS], double now, double dt);

#endif
