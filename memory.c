#include "memory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int bounded(int value, int limit)
{
    return value < 0 ? 0 : value >= limit ? limit - 1 : value;
}

void memory_init(EpisodeMemory *memory)
{
    int c, g, a, i;
    memset(memory, 0, sizeof(*memory));
    for (c = 0; c < PESTINHA_CONTEXT_COUNT; ++c)
        for (g = 0; g < PESTINHA_GOAL_COUNT; ++g)
            for (a = 0; a < PESTINHA_ACTION_COUNT; ++a)
                memory->value[c][g][a] = 0.1;
    for (i = 0; i < 8; ++i) memory->pending_valid[i] = 0;
    memory->metrics.last_action = -1;
    memory->goal.type = 0;
    memory->goal.active = 0;
}

int memory_context(const WorldSnapshot *snapshot)
{
    if (snapshot->corner) return 3;
    if (snapshot->energy < 0.25 || snapshot->asleep) return 1;
    if (snapshot->contact > 0.65) return 2;
    return 0;
}

void memory_record(EpisodeMemory *memory, int context, int goal,
                   int action, double result)
{
    double *value, *count;
    context = bounded(context, PESTINHA_CONTEXT_COUNT);
    goal = bounded(goal, PESTINHA_GOAL_COUNT);
    if (action < 0 || action >= PESTINHA_ACTION_COUNT) return;
    value = &memory->value[context][goal][action];
    count = &memory->count[context][goal][action];
    *count = fmin(40.0, *count + 1.0);
    *value += (result - *value) / *count;
}

int memory_choose(const EpisodeMemory *memory, int context, int goal,
                  int allowed_mask)
{
    int a, c, g, best = -1;
    double local_total = 0.0, global_sum = 0.0, global_count = 0.0;
    double best_score = -1e30;
    context = bounded(context, PESTINHA_CONTEXT_COUNT);
    goal = bounded(goal, PESTINHA_GOAL_COUNT);

    for (c = 0; c < PESTINHA_CONTEXT_COUNT; ++c)
        for (g = 0; g < PESTINHA_GOAL_COUNT; ++g)
            for (a = 0; a < PESTINHA_ACTION_COUNT; ++a) {
                global_sum += memory->value[c][g][a] * memory->count[c][g][a];
                global_count += memory->count[c][g][a];
            }
    for (a = 0; a < PESTINHA_ACTION_COUNT; ++a)
        local_total += memory->count[context][goal][a];
    if (local_total < 2.0 && (allowed_mask & (1 << ACTION_WAIT)))
        return ACTION_WAIT;
    {
        double prior = global_count > 0.0 ? global_sum / global_count : 0.1;
        for (a = 0; a < PESTINHA_ACTION_COUNT; ++a) {
            double n, q, explore, score;
            if (!(allowed_mask & (1 << a))) continue;
            n = memory->count[context][goal][a];
            q = (n * memory->value[context][goal][a] + 5.0 * prior) / (n + 5.0);
            explore = 0.035 * sqrt(log(local_total + 2.0) / (n + 1.0));
            score = q + explore;
            if (score > best_score) { best_score = score; best = a; }
        }
    }
    return best;
}

void memory_begin(EpisodeMemory *memory, int slot, int context, int goal,
                  int action, double predicted, double now)
{
    Experience *e;
    if (slot < 0 || slot >= 8 || action < 0 || action >= PESTINHA_ACTION_COUNT ||
        memory->pending_valid[slot]) return;
    e = &memory->pending[slot];
    e->timestamp = now;
    e->predicted_outcome = predicted;
    e->observed_outcome = 0.0;
    e->valence = 0.0;
    e->confidence = 0.5;
    e->context_id = bounded(context, PESTINHA_CONTEXT_COUNT);
    e->goal_id = bounded(goal, PESTINHA_GOAL_COUNT);
    e->action = action;
    if (memory->metrics.last_action >= 0 &&
        memory->metrics.last_action != action)
        memory->metrics.action_switches++;
    memory->metrics.last_action = action;
    memory->metrics.last_context = e->context_id;
    memory->pending_valid[slot] = 1;
    memory->next_id++;
}

int memory_resolve(EpisodeMemory *memory, int slot, double observed,
                   double now, double *prediction_error)
{
    Experience *e;
    if (slot < 0 || slot >= 8 || !memory->pending_valid[slot]) return 0;
    e = &memory->pending[slot];
    if (now < e->timestamp) return 0;
    e->observed_outcome = fmax(-1.0, fmin(1.0, observed));
    e->valence = e->observed_outcome;
    if (prediction_error) *prediction_error = e->observed_outcome - e->predicted_outcome;
    memory->metrics.absolute_prediction_error +=
        fabs(e->observed_outcome - e->predicted_outcome);
    memory->metrics.resolved_experiences++;
    memory_record(memory, e->context_id, e->goal_id, e->action, e->observed_outcome);
    memory->pending_valid[slot] = 0;
    return 1;
}

void memory_tick(EpisodeMemory *memory, double now)
{
    int i;
    for (i = 0; i < 8; ++i)
        if (memory->pending_valid[i] && now - memory->pending[i].timestamp >= 12.0)
            (void)memory_resolve(memory, i, 0.0, now, NULL);
}

double memory_mean_prediction_error(const EpisodeMemory *memory)
{
    return memory->metrics.resolved_experiences ?
        memory->metrics.absolute_prediction_error /
        (double)memory->metrics.resolved_experiences : 0.0;
}

void memory_consolidate(EpisodeMemory *memory, double now)
{
    int c, g, a;
    if (now - memory->metrics.last_consolidation < 300.0) return;
    for (c = 0; c < PESTINHA_CONTEXT_COUNT; ++c)
        for (g = 0; g < PESTINHA_GOAL_COUNT; ++g)
            for (a = 0; a < PESTINHA_ACTION_COUNT; ++a)
                memory->count[c][g][a] *= 0.995;
    memory->metrics.last_consolidation = now;
}

int memory_save(const EpisodeMemory *memory, const char *path)
{
    char temporary[512];
    FILE *f;
    int c, g, a, e, next;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return 0;
    f = fopen(temporary, "w");
    if (!f) return 0;
    if (fprintf(f, "PESTINHA_MEMORY 5\n%lu %lu %d %d %.9g %.9g %d %.9g %.9g %.9g %.9g %d %.9g %lu\n",
                memory->next_id, memory->metrics.resolved_experiences,
                memory->metrics.last_action, memory->metrics.last_context,
                memory->metrics.absolute_prediction_error,
                memory->metrics.last_consolidation, memory->goal.type,
                memory->goal.priority, memory->goal.urgency,
                memory->goal.progress, memory->goal.deadline,
                memory->goal.active, memory->world_model.prediction_error,
                memory->world_model.prediction_evaluations) < 0) { fclose(f); remove(temporary); return 0; }
    for (c = 0; c < PESTINHA_CONTEXT_COUNT; ++c)
        for (g = 0; g < PESTINHA_GOAL_COUNT; ++g)
            for (a = 0; a < PESTINHA_ACTION_COUNT; ++a)
                if (fprintf(f, "%.9g %.9g\n", memory->value[c][g][a],
                            memory->count[c][g][a]) < 0) {
                    fclose(f); remove(temporary); return 0;
                }
    for (c = 0; c < 8; ++c) {
        const Experience *e = &memory->pending[c];
        if (fprintf(f, "%d %.9g %.9g %.9g %.9g %.9g %d %d %d\n",
                    memory->pending_valid[c], e->timestamp,
                    e->predicted_outcome, e->observed_outcome, e->valence,
                    e->confidence, e->context_id, e->goal_id, e->action) < 0) {
            fclose(f); remove(temporary); return 0;
        }
    }
    for (c = 0; c < 4; ++c)
        for (e = 0; e < EVENT_TYPE_COUNT; ++e) {
            if (fprintf(f, "%lu", memory->world_model.observations[c][e]) < 0) {
                fclose(f); remove(temporary); return 0;
            }
            for (next = 0; next < 4; ++next)
                if (fprintf(f, " %.9g", memory->world_model.transitions[c][e][next]) < 0) {
                    fclose(f); remove(temporary); return 0;
                }
            if (fputc('\n', f) == EOF) { fclose(f); remove(temporary); return 0; }
        }
    if (fclose(f) != 0) { remove(temporary); return 0; }
    if (rename(temporary, path) != 0) { remove(temporary); return 0; }
    return 1;
}

int memory_load(EpisodeMemory *memory, const char *path)
{
    EpisodeMemory loaded;
    FILE *f = fopen(path, "r");
    char magic[32];
    int version, c, g, a, e, next;
    if (!f) return 0;
    memory_init(&loaded);
    if (fscanf(f, "%31s %d", magic, &version) != 2 ||
        strcmp(magic, "PESTINHA_MEMORY") || version != 5) {
        fclose(f); return 0;
    }
    if (fscanf(f, "%lu %lu %d %d %lf %lf %d %lf %lf %lf %lf %d %lf %lu",
               &loaded.next_id, &loaded.metrics.resolved_experiences,
               &loaded.metrics.last_action, &loaded.metrics.last_context,
               &loaded.metrics.absolute_prediction_error,
               &loaded.metrics.last_consolidation, &loaded.goal.type,
               &loaded.goal.priority, &loaded.goal.urgency,
               &loaded.goal.progress, &loaded.goal.deadline,
               &loaded.goal.active, &loaded.world_model.prediction_error,
               &loaded.world_model.prediction_evaluations) != 14 ||
        !isfinite(loaded.metrics.absolute_prediction_error) ||
        !isfinite(loaded.metrics.last_consolidation) ||
        !isfinite(loaded.goal.priority) || !isfinite(loaded.goal.urgency) ||
        !isfinite(loaded.goal.progress) || !isfinite(loaded.goal.deadline) ||
        !isfinite(loaded.world_model.prediction_error) ||
        loaded.metrics.absolute_prediction_error < 0.0 ||
        loaded.world_model.prediction_error < 0.0 ||
        loaded.metrics.last_action < -1 || loaded.metrics.last_action >= PESTINHA_ACTION_COUNT ||
        loaded.metrics.last_context < 0 || loaded.metrics.last_context >= PESTINHA_CONTEXT_COUNT ||
        loaded.goal.type < 0 || loaded.goal.type >= PESTINHA_GOAL_COUNT ||
        (loaded.goal.active != 0 && loaded.goal.active != 1)) {
        fclose(f); return 0;
    }
    for (c = 0; c < PESTINHA_CONTEXT_COUNT; ++c)
        for (g = 0; g < PESTINHA_GOAL_COUNT; ++g)
            for (a = 0; a < PESTINHA_ACTION_COUNT; ++a) {
                double value, count;
                if (fscanf(f, "%lf %lf", &value, &count) != 2 ||
                    !isfinite(value) || !isfinite(count) || count < 0.0 ||
                    count > 40.0 || value < -1.0 || value > 1.0) {
                    fclose(f); return 0;
                }
                loaded.value[c][g][a] = value;
                loaded.count[c][g][a] = count;
            }
    for (c = 0; c < 8; ++c) {
        Experience *pending = &loaded.pending[c];
        if (fscanf(f, "%d %lf %lf %lf %lf %lf %d %d %d",
                   &loaded.pending_valid[c], &pending->timestamp,
                   &pending->predicted_outcome, &pending->observed_outcome, &pending->valence,
                   &pending->confidence, &pending->context_id, &pending->goal_id, &pending->action) != 9 ||
            (loaded.pending_valid[c] != 0 && loaded.pending_valid[c] != 1) ||
            !isfinite(pending->timestamp) || !isfinite(pending->predicted_outcome) ||
            !isfinite(pending->confidence) ||
            pending->context_id < 0 || pending->context_id >= PESTINHA_CONTEXT_COUNT ||
            pending->goal_id < 0 || pending->goal_id >= PESTINHA_GOAL_COUNT ||
            pending->action < 0 || pending->action >= PESTINHA_ACTION_COUNT) {
            fclose(f); return 0;
        }
        if (!isfinite(pending->observed_outcome) || !isfinite(pending->valence) ||
            pending->observed_outcome < -1.0 || pending->observed_outcome > 1.0 ||
            pending->valence < -1.0 || pending->valence > 1.0 ||
            pending->confidence < 0.0 || pending->confidence > 1.0) {
            fclose(f); return 0;
        }
    }
    for (c = 0; c < 4; ++c)
        for (e = 0; e < EVENT_TYPE_COUNT; ++e) {
            if (fscanf(f, "%lu", &loaded.world_model.observations[c][e]) != 1 ||
                loaded.world_model.observations[c][e] > 1000000UL) {
                fclose(f); return 0;
            }
            for (next = 0; next < 4; ++next) {
                double transition;
                if (fscanf(f, "%lf", &transition) != 1 ||
                    !isfinite(transition) || transition < 0.0 || transition > 1000000.0) {
                    fclose(f); return 0;
                }
                loaded.world_model.transitions[c][e][next] = transition;
            }
        }
    fclose(f);
    *memory = loaded;
    return 1;
}
