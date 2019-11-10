
#ifndef EQ3_WIFI_H
#define EQ3_WIFI_H

void initialise_wifi(void);

typedef enum {MQTT_NOT_CONNECTED = 0, MQTT_CONNECTED, MQTT_CONFIG_ERROR}mqttconnstate;
mqttconnstate ismqttconnected(void);

int send_device_list(char *list);
int send_trv_status(char *status);

int connect_server(char *url, char *user, char *password, char *id);

#endif
