#ifndef PESTINHA_RUNTIME_TYPES_H
#define PESTINHA_RUNTIME_TYPES_H

/* Shared runtime state types. Kept independent of the UI and neural engine. */
typedef struct { double energy, stimulation, contact; } Drives;
typedef struct { double valence, arousal, since, override_until; int override; } Emotion;
typedef struct { int current_face, override_face; double override_until; } FaceState;
typedef struct { double cue, end, hit; int pending; } CueEpisode;
typedef enum {
    EVENT_NONE = 0,
    EVENT_CARESS,
    EVENT_POKE,
    EVENT_WORK,
    EVENT_DISMISS,
    EVENT_CORNER,
    EVENT_VISIT,
    EVENT_RELEASE,
    EVENT_MISCHIEF,
    EVENT_THROW,
    EVENT_ADVANCE_TIME,
    EVENT_QUIT,
    EVENT_CLOCK_TICK,
    EVENT_DRIVE_CHANGED,
    EVENT_BEHAVIOR_CHANGED,
    EVENT_TYPE_COUNT
} EventType;
typedef enum {
    ACTION_WANDER = 0,
    ACTION_PLAY,
    ACTION_SETTLE,
    ACTION_REQUEST,
    ACTION_WAIT
} ActionType;
typedef enum {
    EVENT_SOURCE_KEYBOARD = 1,
    EVENT_SOURCE_CLOCK = 2,
    EVENT_SOURCE_INTERNAL = 3
} EventSource;
typedef struct {
    EventType type;
    double timestamp;
    EventSource source;
    double intensity;
    double confidence;
} Event;
typedef struct {
    double timestamp;
    double energy, stimulation, contact;
    double valence, arousal;
    int corner, asleep, sitting, protest;
    int current_goal;
    int context_id;
    int user_present;
    int command_active;
} WorldSnapshot;
typedef struct {
    int type;
    double priority, urgency, progress, deadline;
    int active;
} Goal;
typedef struct {
    int action;
    double expected_value, confidence, cost;
    int allowed;
} ActionProposal;
typedef struct {
    double timestamp, predicted_outcome, observed_outcome, valence, confidence;
    int context_id, goal_id, action;
} Experience;
typedef struct {
    double absolute_prediction_error;
    double last_consolidation;
    unsigned long resolved_experiences;
    unsigned long action_switches;
    int last_action;
    int last_context;
} MemoryMetrics;
typedef struct {
    double transitions[4][EVENT_TYPE_COUNT][4];
    unsigned long observations[4][EVENT_TYPE_COUNT];
    double prediction_error;
    unsigned long prediction_evaluations;
} WorldModel;
typedef struct {
    int corner, asleep, sitting, protest;
    double rancor, fear, escape_memory, escape_will;
    double corner_time, sleep_time, euphoria, request_until, refusal_until;
    double anger_time, protest_time;
    double last_interaction;
    int nook;            /* cantinho de tristeza (motor CANTO); NAO e o castigo */
    double protest_rest; /* descanso (s) antes de um novo protesto no castigo */
} Behavior;
typedef struct {
    double value[4][5][5];
    double count[4][5][5];
    Experience pending[8];
    int pending_valid[8];
    unsigned long next_id;
    MemoryMetrics metrics;
    WorldModel world_model;
    Goal goal;
} EpisodeMemory;

#endif
