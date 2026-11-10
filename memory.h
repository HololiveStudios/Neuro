#ifndef PESTINHA_MEMORY_H
#define PESTINHA_MEMORY_H

#include "runtime_types.h"

#define PESTINHA_ACTION_COUNT 5
#define PESTINHA_CONTEXT_COUNT 4
#define PESTINHA_GOAL_COUNT 5

void memory_init(EpisodeMemory *memory);
int memory_context(const WorldSnapshot *snapshot);
void memory_record(EpisodeMemory *memory, int context, int goal,
                   int action, double result);
int memory_choose(const EpisodeMemory *memory, int context, int goal,
                  int allowed_mask);
void memory_begin(EpisodeMemory *memory, int slot, int context, int goal,
                  int action, double predicted, double now);
int memory_resolve(EpisodeMemory *memory, int slot, double observed,
                   double now, double *prediction_error);
void memory_tick(EpisodeMemory *memory, double now);
double memory_mean_prediction_error(const EpisodeMemory *memory);
void memory_consolidate(EpisodeMemory *memory, double now);
int memory_save(const EpisodeMemory *memory, const char *path);
int memory_load(EpisodeMemory *memory, const char *path);

#endif
