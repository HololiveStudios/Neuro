#include "world.h"

#include <math.h>

WorldSnapshot world_snapshot(double now, const Drives *drives,
                             const Emotion *emotion, const Behavior *behavior,
                             int current_goal)
{
    WorldSnapshot s;
    s.timestamp = now;
    s.energy = drives->energy;
    s.stimulation = drives->stimulation;
    s.contact = drives->contact;
    s.valence = emotion->valence;
    s.arousal = emotion->arousal;
    s.corner = behavior->corner;
    s.asleep = behavior->asleep;
    s.sitting = behavior->sitting;
    s.protest = behavior->protest;
    s.current_goal = current_goal;
    s.user_present = behavior->last_interaction >= 0.0 &&
                     now - behavior->last_interaction <= 30.0;
    s.command_active = now < behavior->request_until;
    s.context_id = behavior->corner ? 3 :
                   (drives->energy < 0.25 || behavior->asleep) ? 1 :
                   drives->contact > 0.65 ? 2 : 0;
    return s;
}

void goal_update(Goal *goal, const WorldSnapshot *snapshot)
{
    int desired = GOAL_IDLE;
    double urgency = 0.0;
    if (snapshot->corner) { desired = GOAL_CALM; urgency = 0.9; }
    else if (snapshot->command_active) { desired = GOAL_FOCUS; urgency = 0.65; }
    else if (snapshot->energy < 0.25) {
        desired = GOAL_REST;
        urgency = (0.25 - snapshot->energy) / 0.25;
    } else if (snapshot->contact > 0.65) {
        desired = GOAL_CONTACT;
        urgency = (snapshot->contact - 0.65) / 0.35;
    }

    /* Keep an active goal through small fluctuations, but release it when its
       need is satisfied or when a clearly stronger need appears. */
    if (goal->active && goal->type != desired) {
        double current_need = 0.0;
        if (goal->type == GOAL_REST) current_need = snapshot->energy < 0.6 ?
            (0.6 - snapshot->energy) / 0.6 : 0.0;
        else if (goal->type == GOAL_CONTACT) current_need = snapshot->contact > 0.25 ?
            (snapshot->contact - 0.25) / 0.75 : 0.0;
        else if (goal->type == GOAL_CALM) current_need = snapshot->corner ? 0.9 : 0.0;
        else if (goal->type == GOAL_FOCUS) current_need = snapshot->command_active ? 0.65 : 0.0;
        if (current_need > 0.0 && urgency < current_need + 0.2 &&
            snapshot->timestamp <= goal->deadline) return;
    }
    if (!goal->active || goal->type != desired) {
        goal->type = desired;
        goal->progress = 0.0;
        goal->deadline = snapshot->timestamp + (desired == GOAL_IDLE ? 30.0 : 120.0);
    }
    goal->urgency = fmax(0.0, fmin(urgency, 1.0));
    goal->priority = desired == GOAL_IDLE ? 0.1 : 0.4 + 0.6 * goal->urgency;
    goal->active = 1;
    goal->progress = desired == GOAL_REST && snapshot->energy > 0.6 ? 1.0 :
                     desired == GOAL_CONTACT && snapshot->contact < 0.25 ? 1.0 :
                     desired == GOAL_CALM && !snapshot->corner ? 1.0 : 0.0;
    if (goal->progress >= 1.0 || snapshot->timestamp > goal->deadline) {
        goal->active = 0;
        goal->type = GOAL_IDLE;
    }
}

double event_attention(const Event *event, const WorldSnapshot *snapshot,
                       const WorldModel *model)
{
    double urgency = event->type == EVENT_QUIT ? 1.0 :
                     event->type == EVENT_DISMISS || event->type == EVENT_CORNER ? 0.9 :
                     event->type == EVENT_CARESS || event->type == EVENT_POKE ? 0.7 : 0.3;
    double novelty = 1.0 - world_model_confidence(model, snapshot->context_id,
                                                  event->type);
    double relevance = (snapshot->corner && event->type == EVENT_RELEASE) ? 1.0 : 0.5;
    double predicted_confidence = 0.0;
    int predicted_context = world_model_predict(model, snapshot->context_id,
                                               event->type, &predicted_confidence);
    if (predicted_context != snapshot->context_id)
        relevance = fmax(relevance, 0.5 + 0.5 * predicted_confidence);
    return fmax(0.0, fmin(1.0, urgency * 0.45 + novelty * 0.25 +
                          relevance * 0.30 * event->confidence));
}

void world_model_observe(WorldModel *model, int from_context, EventType event,
                         int to_context)
{
    unsigned long *count;
    double *transition;
    double total = 0.0;
    int i;
    if (event <= EVENT_NONE || event >= EVENT_TYPE_COUNT) return;
    if (from_context < 0 || from_context >= 4 || to_context < 0 || to_context >= 4) return;
    count = &model->observations[from_context][event];
    transition = &model->transitions[from_context][event][to_context];
    for (i = 0; i < 4; ++i)
        total += model->transitions[from_context][event][i];
    if (*count > 0) {
        double probability = (*transition + 1.0) / (total + 4.0);
        model->prediction_error += 1.0 - probability;
        model->prediction_evaluations++;
    }
    *count = *count < 1000000UL ? *count + 1UL : *count;
    *transition += 1.0;
}

double world_model_confidence(const WorldModel *model, int context,
                              EventType event)
{
    unsigned long count;
    if (context < 0 || context >= 4 || event <= EVENT_NONE || event >= EVENT_TYPE_COUNT) return 0.0;
    count = model->observations[context][event];
    return (double)count / ((double)count + 3.0);
}

int world_model_predict(const WorldModel *model, int context, EventType event,
                        double *confidence)
{
    int i, best = context;
    double best_count = 0.0, total = 0.0;
    if (context < 0 || context >= 4 || event <= EVENT_NONE || event >= EVENT_TYPE_COUNT) {
        if (confidence) *confidence = 0.0;
        return context;
    }
    for (i = 0; i < 4; ++i) {
        double n = model->transitions[context][event][i];
        total += n;
        if (n > best_count) { best_count = n; best = i; }
    }
    if (confidence) *confidence = total > 0.0 ? best_count / total : 0.0;
    return best;
}
