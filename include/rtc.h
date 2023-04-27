
/* RTC: Microcrystal RV-3149-C3
 * https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-3149-C3_App-Manual.pdf
 */

#ifndef RTC_H_
#define RTC_H_
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "config.h"
#include "spi.h"
#include "gpio.h"
#include "interrupts.h"
#include "uart.h"

#define RTC_SPI LPSPI2

typedef struct {
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} rtc_date_time_t;

typedef void (*rtc_tick_hook_t)(void); //Function pointer for the tick hook

void init_rtc(rtc_tick_hook_t rtcTickHook);
void rtc_sync(void);

rtc_date_time_t get_rtc_date_time(void);
void rtc_set_date_time(rtc_date_time_t *dateTime);
char* rtc_get_timestamp(void);
void rtc_tick_task(void *p);
void rtc_print_time(void);

uint32_t uptime_in_100_ms(void);

#endif /* RTC_H_ */
