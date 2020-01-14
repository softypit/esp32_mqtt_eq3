#ifndef EQ3_MAIN_H
#define EQ3_MAIN_H

#define EQ3_MAJVER "1"
#define EQ3_MINVER "51"
#define EQ3_EXTRAVER "-beta"

void eq3_log_init(void);
void eq3_add_log(char *log);

int handle_request(char *cmdstr);

void schedule_reboot(void);

/* LOLIN_OLED can be defined if using a LOLIN OLED ESP32 board */
/* This uses the https://github.com/TaraHoleInIt/tarablessd1306 SSD1306 driver */
#define LOLIN_OLED

#ifdef LOLIN_OLED
void mqttdone(bool connected);
#endif

#endif 
