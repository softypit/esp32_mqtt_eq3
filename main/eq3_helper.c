#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "eq3_helper.h"

#define HELPER_TAG "EQ3_HELPER"

bool isLegacyMqttUrl( const char s[] )
{
	char *index = strstr(s, "://");

    return (index == NULL);
}
