#include <measurement_event.h>


#define MODULE sensor_sim

static bool event_handler(const struct event_header *eh)    //function is called whenever the subscribed events is being processed
{
	if (is_measurement_event(eh)) {
		struct measurement_event *event = cast_measurement_event(eh);

		s8_t v1 = event->value1;
		// s16_t v2 = event->value2;
		// s32_t v3 = event->value3;

        // switch (v1) { //eh->type
        //     case GPS_DATA_READY:
        //         printk("GPS_DATA_READY EVENT triggered");
        //         break;
        //     default:
        //         printk("RECIEVED UNKNOWN EVENT");
        //         break;
        // }

		printk("Value to be passed to cloud %d", v1);

        //Execute some action here

		return false;
	}

	return false;
}

EVENT_LISTENER(MODULE, event_handler);  //defining module as listener
EVENT_SUBSCRIBE(MODULE, measurement_event); //defining module as subscriber

/*
 * 
 * EVENT_SUBSCRIBE_EARLY - notification before other listeners
 * EVENT_SUBSCRIBE - standard notification
 * EVENT_SUBSCRIBE_FINAL - notification as last, final subscriber
 */ 


