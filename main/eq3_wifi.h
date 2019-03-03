
#ifndef EQ3_WIFI_H
#define EQ3_WIFI_H

#define EQ3_MAJVER "1"
#define EQ3_MINVER "28"

void initialise_wifi(void);

bool ismqttconnected(void);

int send_device_list(char *list);
int send_trv_status(char *status);

int connect_server(char *url, char *user, char *password, char *id);

/* This shouldn't be here!! */
int handle_request(char *cmdstr);

#endif
