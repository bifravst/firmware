#include "event_manager.h"

//#define GPS_DATA_READY 1

enum GPS_management_evt_type {
    GPS_REQ_DATA,
    PLACE_HOLDER_1,
    PLACE_HOLDER_2
};

struct measurement_event {
    struct event_header header;

    enum GPS_management_evt_type type;
    //s8_t value1;
    //s16_t value2;
    //s32_t value3;

};

EVENT_TYPE_DECLARE(measurement_event);