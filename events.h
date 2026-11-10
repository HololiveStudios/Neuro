#ifndef PESTINHA_EVENTS_H
#define PESTINHA_EVENTS_H

#include "runtime_types.h"

Event event_from_key(char key, double timestamp);
Event event_internal(EventType type, double timestamp, double intensity,
                     double confidence);

#endif
