
#ifndef EQ3_TIMER_H
#define EQ3_TIMER_H


typedef struct {
    int type;                  /*!< event type */
    int group;                 /*!< timer group */
    int idx;                   /*!< timer number */
    uint64_t counter_val;      /*!< timer counter value */
} timer_event_t;

int init_timer(xQueueHandle informqueue);
int start_timer(unsigned int delayMS);

bool timer_running(void);

#endif
