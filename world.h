#ifndef PESTINHA_WORLD_H
#define PESTINHA_WORLD_H

#include "runtime_types.h"

enum { GOAL_IDLE = 0, GOAL_REST = 1, GOAL_CONTACT = 2, GOAL_CALM = 3, GOAL_FOCUS = 4 };

WorldSnapshot world_snapshot(double now, const Drives *drives,
                             const Emotion *emotion, const Behavior *behavior,
                             int current_goal);
void goal_update(Goal *goal, const WorldSnapshot *snapshot);
double event_attention(const Event *event, const WorldSnapshot *snapshot,
                       const WorldModel *model);
void world_model_observe(WorldModel *model, int from_context, EventType event,
                         int to_context);
double world_model_confidence(const WorldModel *model, int context,
                              EventType event);
int world_model_predict(const WorldModel *model, int context, EventType event,
                        double *confidence);

#endif
