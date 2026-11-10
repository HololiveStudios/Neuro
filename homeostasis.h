#ifndef PESTINHA_HOMEOSTASIS_H
#define PESTINHA_HOMEOSTASIS_H

#include "neuro.h"
#include "runtime_types.h"

void homeostasis_event(Drives *drives, const char *kind);
void homeostasis_update(Drives *drives, const Behavior *behavior,
                        double dt, int moving, double arousal, int away,
                        int engaged, int visited,
                        double inputs[PESTINHA_CHANNELS]);

#endif
