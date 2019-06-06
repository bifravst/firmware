#include <stdio.h>
#include "measurement_event.h"

static int log_measurement_event(const struct event_header *eh, char *buf, size_t buf_len) {    //module which logs gps data
    struct measurement_event *event = cast_measurement_event(eh);
    return snprintf(buf, buf_len, "val1=%d", event->value1);
}


EVENT_TYPE_DEFINE(measurement_event, true, log_measurement_event, NULL);