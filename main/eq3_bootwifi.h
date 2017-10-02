/*
 * bootwifi.h
 *
 *  Created on: Nov 25, 2016
 *      Author: kolban
 */

#ifndef MAIN_BOOTWIFI_H_
#define MAIN_BOOTWIFI_H_

typedef void (*bootwifi_callback_t)(int rc);
typedef void (*bootwifi_parms_t)(char *, char *, char *);
void bootWiFi();


#endif /* MAIN_BOOTWIFI_H_ */
