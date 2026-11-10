#include "behavior.h"

#include <math.h>

static const int face_priority[] = {20,20,20,20,20,20,40,35,50,15,20,30};

static double clamp_value(double value, double low, double high)
{
    return fmax(low, fmin(value, high));
}

static void set_input(double inputs[PESTINHA_CHANNELS], const char *name,
                      double value)
{
    int channel = brain_channel_index(name);
    if (channel >= 0) inputs[channel] = clamp_value(value, 0.0, 1.0);
}

void face_force(FaceState *face, double now, int index, double seconds)
{
    if (index < 0 || index >= (int)(sizeof(face_priority) / sizeof(face_priority[0]))) return;
    if (face->override_face >= 0 && now < face->override_until &&
        face_priority[index] < face_priority[face->override_face]) return;
    face->override_face = index;
    face->override_until = now + seconds;
}

void behavior_update_emotion(Emotion *emotion, Behavior *behavior,
                             FaceState *face, const Brain *brain,
                             double now, double dt)
{
    static const double valence[8] = {-.2,0,.25,.9,.6,0,-.9,-.9};
    static const double arousal[8] = {1,.3,.55,.8,-.45,-.8,.9,-.6};
    int k;
    double sumv = 0.0, suma = 0.0, weight = 2.0;
    for (k = 0; k < 8; ++k) {
        double h = fmax(0.0, brain_motor(brain, k) - 0.5);
        sumv += h * valence[k];
        suma += h * arousal[k];
        weight += h;
    }
    {
        double rate = 1.0 - exp(-dt / 5.0);
        emotion->valence += (sumv / weight - emotion->valence) * rate;
        emotion->arousal += (suma / weight - emotion->arousal) * rate;
    }
    if (brain_motor(brain, 0) >= 7.0 && face->current_face != 6)
        face_force(face, now, 6, 1.2);
    if (behavior->asleep) { face->current_face = 8; return; }
    if (behavior->protest) { face->current_face = 10; return; }
    if (face->override_face >= 0 && now < face->override_until)
        face->current_face = face->override_face;
    else {
        double v = emotion->valence, a = emotion->arousal;
        int next;
        face->override_face = -1;
        next = v < -.2 ? (a > .15 ? 4 : 5) :
               v > .18 ? (a > .3 ? 2 : 1) : a > .22 ? 3 : 0;
        if (next != face->current_face) face->current_face = next;
    }
}

void behavior_update(Behavior *behavior, Drives *drives,
                     ConditioningState *conditioning, const Brain *brain,
                     double inputs[PESTINHA_CHANNELS], double now, double dt)
{
    double sit = brain_motor(brain, 8), flee = brain_motor(brain, 9);
    double accept = brain_motor(brain, 14), angry = brain_motor(brain, 6);
    double calm = brain_motor(brain, 4);
    if (now < behavior->request_until)
        set_input(inputs, "PEDIDO", clamp_value((behavior->request_until - now) / 10.0, 0.0, 1.0));
    else set_input(inputs, "PEDIDO", 0.0);
    /* CANTO (tristeza) leva ao cantinho de tristeza. O castigo (behavior->corner)
       so comeca quando o usuario coloca o pet la (EVENT_CORNER). */
    if (brain_motor(brain, 11) >= CANTO_ENTER_HZ) behavior->nook = 1;
    else if (brain_motor(brain, 11) < CANTO_LEAVE_HZ) behavior->nook = 0;
    (void)conditioning;
    if (behavior->corner) {
        behavior->corner_time += dt;
        behavior->rancor = clamp_value(behavior->rancor + dt / 120.0, 0.0, 1.0);
        if (accept >= ACCEPT_FORGIVE_HZ) {
            behavior->corner = 0; behavior->rancor *= .5;
            behavior->escape_will = 0.0;
            behavior->fear = fmax(0.0, behavior->fear - .1);
        } else {
            behavior->escape_will = fmax(0.0, behavior->escape_will + dt * (flee - 2.0) / 300.0);
            if (behavior->escape_will >= 1.0) {
                behavior->corner = 0; behavior->escape_will = 0.0;
                behavior->escape_memory = clamp_value(behavior->escape_memory + .2, 0.0, 1.0);
            }
            if (brain_motor(brain, 10) >= CARTAZ_PROTEST_HZ &&
                behavior->corner_time > CARTAZ_PROTEST_AFTER &&
                behavior->protest_rest <= 0.0)
                behavior->protest = 1;
        }
    } else behavior->rancor = fmax(0.0, behavior->rancor - dt / 600.0);

    set_input(inputs, "PRESO", behavior->corner);
    set_input(inputs, "RANCOR", behavior->rancor);
    set_input(inputs, "RECEIO", behavior->fear);
    set_input(inputs, "LEMBRA_FUGA", behavior->escape_memory);
    if (sit >= 6.0) behavior->sitting = 1;
    else if (sit < 3.0) behavior->sitting = 0;
    if (angry > 15.0) behavior->anger_time += dt;
    else behavior->anger_time = fmax(0.0, behavior->anger_time - dt * .5);
    if (behavior->anger_time > 15.0) behavior->protest = 1;
    behavior->protest_rest = fmax(0.0, behavior->protest_rest - dt);
    if (behavior->protest) behavior->protest_time += dt;
    if (behavior->protest_time > 30.0) {
        behavior->protest = 0; behavior->protest_time = 0.0; behavior->anger_time = 0.0;
        behavior->protest_rest = PROTEST_REST_SECONDS;
    }
    if (drives->energy < .18) behavior->asleep = 1;
    else if (drives->energy >= .6) behavior->asleep = 0;
    if (behavior->asleep) behavior->sleep_time += dt;
    if (calm > 3.0 && brain_motor(brain, 3) > 6.0)
        behavior->euphoria = clamp_value(behavior->euphoria + dt * .1 *
            (brain_motor(brain, 3) - .5) / 6.0, 0.0, 1.5);
    else behavior->euphoria = fmax(0.0, behavior->euphoria - dt * .008);
}
