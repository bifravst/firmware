#include <string_manipulation.h>
#include <string.h>
#include <stdio.h>

#define PARAMETER_SPLITTER  ","
#define IDENTIFICATION      "ID:"
#define BATTERY_STATUS      "BAT:"
#define GPS_DATA            "GPS:"

void concat_structure(char *mqtt_assembly_line_d, char *addition)
{
	if ((mqtt_assembly_line_d[0] == '\0') && (addition[0] == 'C') &&
	    (addition[1] == 'T')) {
		strcat(mqtt_assembly_line_d, IDENTIFICATION);
		strcat(mqtt_assembly_line_d, addition);
		strcat(mqtt_assembly_line_d, PARAMETER_SPLITTER);
	} else if ((addition[0] == '$') && (addition[1] == 'G')) {
        strcat(mqtt_assembly_line_d, GPS_DATA);
		strcat(mqtt_assembly_line_d, addition);
	} else {
		strcat(mqtt_assembly_line_d, BATTERY_STATUS);
		strcat(mqtt_assembly_line_d, addition);
		strcat(mqtt_assembly_line_d, PARAMETER_SPLITTER);
	}

}

void delete_publish_data(char *mqtt_assembly_line_d) {
	memset(mqtt_assembly_line_d, 0, strlen(mqtt_assembly_line_d));
    //need to introduce id again somehow
}
