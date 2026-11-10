#include "conditioning.h"

#include <math.h>

static double clamp_unit(double value)
{
    return fmax(0.0, fmin(value, 1.0));
}

void conditioning_init(ConditioningState *state)
{
    int i;
    for (i = 0; i < PESTINHA_CUE_COUNT; ++i) {
        state->episode[i].cue = 0.0;
        state->episode[i].end = 0.0;
        state->episode[i].hit = 0.0;
        state->episode[i].pending = 0;
    }
    state->press_value = 0.0;
    state->approach_value = 0.0;
    state->plaque_value = 0.0;
}

void conditioning_start(ConditioningState *state, int cue, double now)
{
    CueEpisode *episode;
    if (cue < 0 || cue >= PESTINHA_CUE_COUNT) return;
    episode = &state->episode[cue];
    if (!episode->pending) {
        episode->cue = now;
        episode->hit = 0.0;
        episode->pending = 1;
    }
}

void conditioning_end(ConditioningState *state, int cue, double now)
{
    CueEpisode *episode;
    if (cue < 0 || cue >= PESTINHA_CUE_COUNT) return;
    episode = &state->episode[cue];
    if (episode->pending) {
        episode->end = now;
        episode->pending = 2;
    }
}

void conditioning_outcome(ConditioningState *state, int outcome, double now)
{
    int i;
    for (i = 0; i < PESTINHA_CUE_COUNT; ++i) {
        CueEpisode *episode = &state->episode[i];
        int matches = (outcome == 2 && i == 2) ||
                      (outcome != 2 && i < 2);
        if (episode->pending && matches && now - episode->cue < 8.0)
            episode->hit = 1.0;
    }
}

void conditioning_update(ConditioningState *state, double now)
{
    int i;
    for (i = 0; i < PESTINHA_CUE_COUNT; ++i) {
        CueEpisode *episode = &state->episode[i];
        double delay = i == 2 ? 4.0 : 1.5;
        double *value;
        if (episode->pending != 2 || now - episode->end <= delay) continue;
        value = i == 0 ? &state->press_value :
                i == 1 ? &state->approach_value : &state->plaque_value;
        *value = clamp_unit(*value + 0.12 * (episode->hit - *value));
        episode->pending = 0;
    }
}
