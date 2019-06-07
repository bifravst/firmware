#include <measurement_event.h>
#include <gps_func.h>


#define MODULE GPS_management

static bool GPS_event_handler(const struct event_header *eh)    //function is called whenever the subscribed events is being processed
{

	if (is_measurement_event(eh)) {
		struct measurement_event *event = cast_measurement_event(eh);

        switch (event->type) { //eh->type
            case GPS_REQ_DATA:
                //printk("GPS_REQ_DATA EVENT triggered in the GPS module\n");
                dummy_function_gps();
                break;
            default:
                printk("RECIEVED UNKNOWN EVENT");
                break;
        }

		return false;
	}

	return false;
}

EVENT_LISTENER(MODULE, GPS_event_handler);  //defining module as listener
EVENT_SUBSCRIBE(MODULE, measurement_event); //defining module as subscriber

/*
 * 
 * EVENT_SUBSCRIBE_EARLY - notification before other listeners
 * EVENT_SUBSCRIBE - standard notification
 * EVENT_SUBSCRIBE_FINAL - notification as last, final subscriber
 */ 


