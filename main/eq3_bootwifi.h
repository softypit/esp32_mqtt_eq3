/*
 * bootwifi.h
 *
 *  Created on: Nov 25, 2016
 *      Author: kolban
 */

#ifndef MAIN_BOOTWIFI_H_
#define MAIN_BOOTWIFI_H_

typedef void (*bootwifi_callback_t)(int rc);
typedef void (*bootwifi_parms_t)(char *, char *, char *, char *);
void bootWiFi();
void restart_station(void);

bool ntp_enabled(void);
char *getntpserver(int idx);
char *getntptimezone(void);

#endif /* MAIN_BOOTWIFI_H_ */
