#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_gatt_defs.h"
#include "eq3_utils.h"

static char uuidToStringBuffer[ESP_UUID_LEN_128 * 2 + 1];
char *UuidToString(const esp_bt_uuid_t id)
{
    int bytecount, writecount = 0;
    for (bytecount = ESP_UUID_LEN_128 - 1; bytecount >= 0; bytecount--, writecount += 2)
    {
        sprintf(&uuidToStringBuffer[writecount], "%02x", id.uuid.uuid128[bytecount] & 0xff);
    }
    return uuidToStringBuffer;
}