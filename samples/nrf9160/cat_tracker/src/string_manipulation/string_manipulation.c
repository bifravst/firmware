#include <string_manipulation.h>
#include <string.h>
#include <stdio.h>

#define BEGIN_LONGITUDE 17
#define END_LONGITUDE	26
#define BEGIN_LATITUDE	30
#define END_LATITUDE	40

void removeChar(char *s, int c)
{
	int j, n = strlen(s);
	for (int i = j = 0; i < n; i++)
		if (s[i] != c)
			s[j++] = s[i];

	s[j] = '\0';
}

void concat_structure(char *mqtt_assembly_line_d, char *addition)
{
	char longitude[50] = "";
	char latitude[50] = "";

	if ((addition[0] == '$') && (addition[1] == 'G')) {

		for (int i = BEGIN_LONGITUDE; i < END_LONGITUDE; i++) {
			longitude[i - BEGIN_LONGITUDE] = addition[i];
		}

		for (int j = BEGIN_LATITUDE; j < END_LATITUDE; j++) {
			latitude[j - BEGIN_LATITUDE] = addition[j];
		}

		removeChar(longitude, '.');
		removeChar(latitude, '.');

		strcat(mqtt_assembly_line_d, longitude);
		strcat(mqtt_assembly_line_d, latitude);

	} else {
		strcat(mqtt_assembly_line_d, addition);
	}

}

void delete_publish_data(char *mqtt_assembly_line_d) {
	memset(mqtt_assembly_line_d, 0, strlen(mqtt_assembly_line_d));
}
