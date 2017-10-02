
#ifndef EQ3_WIFI_H
#define EQ3_WIFI_H

void initialise_wifi(void);

int send_device_list(char *list);
int send_trv_status(char *status);

int connect_server(char *url, char *user, char *password);

/* This shouldn't be here!! */
int handle_request(char *cmdstr);

#endif
