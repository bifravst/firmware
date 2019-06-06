#include "event_manager.h"

//#define GPS_DATA_READY 1

// enum asset_tracker_evt_type {
//     GPS_DATA_READY,
//     GPS_IDLE,
// };

struct measurement_event {
    struct event_header header;

    //enum asset_tracker_evt_type type;
    s8_t value1;
    //s16_t value2;
    //s32_t value3;

};

EVENT_TYPE_DECLARE(measurement_event);