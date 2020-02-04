#ifndef EQ3_UTILS_H
#define EQ3_UTILS_H

char *UuidToString(const esp_bt_uuid_t id);
bool compare_uuid(const esp_bt_uuid_t id, const esp_bt_uuid_t expectedServiceId);

#endif