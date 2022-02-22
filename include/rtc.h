#ifndef RTC_H_
#define RTC_H_
#include <stdint.h>
#include <string.h>
#include "S32K146.h"
#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "spi.h"
#include "gpio.h"
#include "interrupts.h"

#define RTC_SPI LPSPI1

typedef struct {
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} rtc_date_time_t;

typedef void (*rtc_tick_hook_t)(void); //Function pointer for the tick hook

extern rtc_date_time_t rtcDateTime;

void rtc_register_tick_hook(rtc_tick_hook_t rtcTickHook);
void rtc_init(void);
void rtc_sync(void);
BaseType_t rtc_date_time_mutex_take(TickType_t blocktime);
void rtc_date_time_mutex_give(void);
void rtc_set_date_time(rtc_date_time_t *dateTime);
char* rtc_get_timestamp(TickType_t blocktime);
void rtc_tick_task(void *p);


#endif /* RTC_H_ */
