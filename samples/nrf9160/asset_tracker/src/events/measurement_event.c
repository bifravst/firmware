#include <stdio.h>
#include "measurement_event.h"

// static int log_measurement_event(const struct event_header *eh, char *buf, size_t buf_len) {    //module which logs gps data
//     struct measurement_event *event = cast_measurement_event(eh);
//     return snprintf(buf, buf_len, "Event type=%d", event->type);
// }

// static testing_function(const struct even_header *eh) {
    
// }




EVENT_TYPE_DEFINE(measurement_event, false, NULL, NULL); 

/* Unique event name. */
/* Event logged by default. */
/* Function logging event data. This section could contain any function? */
/* No event info provided. */