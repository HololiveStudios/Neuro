#include "events.h"

Event event_from_key(char key, double timestamp)
{
    Event event;
    event.type = EVENT_NONE;
    event.timestamp = timestamp;
    event.source = EVENT_SOURCE_KEYBOARD;
    event.intensity = 1.0;
    event.confidence = 1.0;

    switch (key) {
    case 'c': event.type = EVENT_CARESS; break;
    case 'p': event.type = EVENT_POKE; event.intensity = 0.8; break;
    case 'w': event.type = EVENT_WORK; break;
    case 'x': event.type = EVENT_DISMISS; event.intensity = 0.7; break;
    case 'l': event.type = EVENT_CORNER; break;
    case 'v': event.type = EVENT_VISIT; event.intensity = 0.5; break;
    case 'f': event.type = EVENT_RELEASE; break;
    case 'm': event.type = EVENT_MISCHIEF; break;
    case 'a': event.type = EVENT_THROW; event.intensity = 0.8; break;
    case 't': event.type = EVENT_ADVANCE_TIME; break;
    case 'q': event.type = EVENT_QUIT; break;
    default: break;
    }
    return event;
}

Event event_internal(EventType type, double timestamp, double intensity,
                     double confidence)
{
    Event event;
    event.type = type;
    event.timestamp = timestamp;
    event.source = type == EVENT_CLOCK_TICK ? EVENT_SOURCE_CLOCK : EVENT_SOURCE_INTERNAL;
    event.intensity = intensity < 0.0 ? 0.0 : intensity > 1.0 ? 1.0 : intensity;
    event.confidence = confidence < 0.0 ? 0.0 : confidence > 1.0 ? 1.0 : confidence;
    return event;
}
