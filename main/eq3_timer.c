/* 
 * Timer code for eq-3 interface
 * 
 * Borrowed from Espressiv timer example code
 *
 * updated by Peter Becker (C) 2019
 */

#include <stdio.h>
#include "esp_types.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "driver/uart.h"

#include "eq3_timer.h"

#define TIMER_INTR_SEL TIMER_INTR_LEVEL  /*!< Timer level interrupt */
#define TIMER_GROUP    TIMER_GROUP_0     /*!< Test on timer group 0 */
#define TIMER_DIVIDER   16               /*!< Hardware timer clock divider */
#define TIMER_SCALE    (TIMER_BASE_CLK / TIMER_DIVIDER)  /*!< used to calculate counter value */
#define TIMER_SCALE_MS (TIMER_BASE_CLK / (TIMER_DIVIDER * 1000))
#define TIMER_FINE_ADJ   (1.4*(TIMER_BASE_CLK / TIMER_DIVIDER)/1000000) /*!< used to compensate alarm value */
#define TIMER_INTERVAL0_SEC   (3.4179)   /*!< test interval for timer 0 */
#define TIMER_INTERVAL1_SEC   (5.78)   /*!< test interval for timer 1 */
#define TEST_WITHOUT_RELOAD   0   /*!< example of auto-reload mode */
#define TEST_WITH_RELOAD   1      /*!< example without auto-reload mode */

bool timer0running = false;

xQueueHandle timer_queue;

/*
 * @brief timer group0 ISR handler
 */
void IRAM_ATTR timer_group0_isr(void *para)
{
    int timer_idx = (int) para;
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    timer_event_t evt;
    if((intr_status & BIT(timer_idx)) && timer_idx == TIMER_0) {
        /*Timer0 is an example that doesn't reload counter value*/
        TIMERG0.hw_timer[timer_idx].update = 1;

        /* We don't call a API here because they are not declared with IRAM_ATTR.
           If we're okay with the timer irq not being serviced while SPI flash cache is disabled,
           we can alloc this interrupt without the ESP_INTR_FLAG_IRAM flag and use the normal API. */
        TIMERG0.int_clr_timers.t0 = 1;
        uint64_t timer_val = ((uint64_t) TIMERG0.hw_timer[timer_idx].cnt_high) << 32 | TIMERG0.hw_timer[timer_idx].cnt_low;

        timer0running = false;

        /*Post an event to out example task*/
        evt.type = TEST_WITHOUT_RELOAD;
        evt.group = 0;
        evt.idx = timer_idx;
        evt.counter_val = timer_val;
        xQueueSendFromISR(timer_queue, &evt, NULL);

        /*Enable timer interrupt*/
        timer_disable_intr(TIMER_GROUP_0, timer_idx);

    } 
}

int init_timer(xQueueHandle informqueue){
    int timer_group = TIMER_GROUP_0;
    int timer_idx = TIMER_0;
    timer_queue = informqueue;
    timer_config_t config;
    config.alarm_en = 1;
    config.auto_reload = 0;
    config.counter_dir = TIMER_COUNT_UP;
    config.divider = TIMER_DIVIDER;
    config.intr_type = TIMER_INTR_SEL;
    config.counter_en = TIMER_PAUSE;
    /*Configure timer*/
    timer_init(timer_group, timer_idx, &config);
    /*Set ISR handler*/
    timer_isr_register(timer_group, timer_idx, timer_group0_isr, (void*) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

    return 0;
}

bool timer_running(void){
    return timer0running;
}

#define TIMER_TAG "tmr"

int start_timer(unsigned int delayMS){
    int timer_group = TIMER_GROUP_0;
    int timer_idx = TIMER_0;
    unsigned long long delay = delayMS;

    ESP_LOGI(TIMER_TAG, "Start timer %d mS", delayMS);

    timer_pause(timer_group, timer_idx);
    /*Enable timer interrupt*/
    timer_enable_intr(timer_group, timer_idx);
    /*Load counter value */
    timer_set_counter_value(timer_group, timer_idx, 0x00000000ULL);
    /*Set alarm value*/
    //timer_set_alarm_value(timer_group, timer_idx, TIMER_INTERVAL0_SEC * TIMER_SCALE - TIMER_FINE_ADJ);
    timer_set_alarm_value(timer_group, timer_idx, delay * TIMER_SCALE_MS);
    /* Enable the alarm */
    timer_set_alarm(timer_group, timer_idx, 1);
    timer0running = true;
    /*Start timer counter*/
    timer_start(timer_group, timer_idx);

    return 0;
}

