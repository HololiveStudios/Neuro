#include "homeostasis.h"

#include <math.h>
#include <string.h>

static double clamp_value(double value, double low, double high)
{
    return fmax(low, fmin(value, high));
}

static void set_channel(double inputs[PESTINHA_CHANNELS], const char *name,
                        double value)
{
    int index = brain_channel_index(name);
    if (index >= 0) inputs[index] = clamp_value(value, 0.0, 1.0);
}

void homeostasis_event(Drives *drives, const char *kind)
{
    if (!strcmp(kind, "app")) drives->stimulation += 0.08;
    else if (!strcmp(kind, "novelty")) drives->stimulation += 0.2;
    else if (!strcmp(kind, "poke")) {
        drives->stimulation += 0.15; drives->contact -= 0.15;
    } else if (!strcmp(kind, "drag")) {
        drives->stimulation += 0.2; drives->contact -= 0.1;
    } else if (!strcmp(kind, "throw")) {
        drives->stimulation += 0.35; drives->contact -= 0.1;
    } else if (!strcmp(kind, "mischief")) drives->stimulation += 0.25;
    else if (!strcmp(kind, "caress")) {
        drives->stimulation += 0.05; drives->contact -= 0.25;
    } else if (!strcmp(kind, "dismiss")) drives->contact -= 0.05;
    drives->stimulation = clamp_value(drives->stimulation, 0.0, 1.0);
    drives->contact = clamp_value(drives->contact, 0.0, 1.0);
}

void homeostasis_update(Drives *drives, const Behavior *behavior,
                        double dt, int moving, double arousal, int away,
                        int engaged, int visited,
                        double inputs[PESTINHA_CHANNELS])
{
    double effort = 1.0 + 1.5 * moving + fmax(0.0, arousal);
    if (behavior->asleep)
        drives->energy = clamp_value(drives->energy + dt / 1200.0, 0.0, 1.0);
    else
        drives->energy = clamp_value(drives->energy - dt * effort / 10800.0, 0.0, 1.0);
    if (engaged && !behavior->asleep)
        drives->stimulation += (0.6 - drives->stimulation) * fmin(1.0, dt / 20.0);
    else
        drives->stimulation = clamp_value(drives->stimulation -
            dt * (behavior->corner ? 3.0 : 1.0) / 240.0, 0.0, 1.0);
    drives->contact = clamp_value(drives->contact + dt / 2400.0 *
        (away ? 0.3 : (behavior->corner && !visited ? 2.0 : 1.0)), 0.0, 1.0);
    set_channel(inputs, "BOREDOM", clamp_value((0.35 - drives->stimulation) / 0.35, 0.0, 1.0));
    set_channel(inputs, "LONELY", clamp_value((drives->contact - 0.5) / 0.5, 0.0, 1.0));
    set_channel(inputs, "FATIGUE", clamp_value((0.5 - drives->energy) / 0.5, 0.0, 1.0));
}
