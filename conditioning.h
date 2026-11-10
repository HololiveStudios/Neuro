#ifndef PESTINHA_CONDITIONING_H
#define PESTINHA_CONDITIONING_H

#include "runtime_types.h"

#define PESTINHA_CUE_COUNT 8

typedef struct {
    CueEpisode episode[PESTINHA_CUE_COUNT];
    double press_value;
    double approach_value;
    double plaque_value;
} ConditioningState;

void conditioning_init(ConditioningState *state);
void conditioning_start(ConditioningState *state, int cue, double now);
void conditioning_end(ConditioningState *state, int cue, double now);
void conditioning_outcome(ConditioningState *state, int outcome, double now);
void conditioning_update(ConditioningState *state, double now);

#endif
