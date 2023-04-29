/*!
 * @file            rtc.h
 * @brief           Library which interfaces with Microcrystal RV-3149-C3 RTC.
 * This Library is not really hardware-independent and relies heavily on the driver modules for the S32K14x
 * @note            Reference Manual: https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-3149-C3_App-Manual.pdf
 */

/*
Copyright 2023 Luca Engelmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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

/*!
 * @def RTC_SPI
 * Define the SPI peripheral used by the RTC
 */
#define RTC_SPI LPSPI2

/*!
 * @struct rtc_date_time_t
 * @brief Data type for the date and time
 */
typedef struct {
	uint8_t day;    //!< Day: Starting at 1 for the first day
	uint8_t month;  //!< Month: Starting at 1 for January
	uint16_t year;  //!< Year in format YYYY
	uint8_t hour;   //!< Hour
	uint8_t minute; //!< Minute
	uint8_t second; //!< Second
} rtc_date_time_t;

/*!
 * @brief Function pointer declaration for the tick hook function.
 * The tick hook will be executed every 100 ms and transmits the current uptime as argument.
 * @param uptime Uptime in 100 ms intervals
 * @note Keep the called function as short as possible!
 */
typedef void (*rtc_tick_hook_t)(uint32_t uptime);

/*!
 *  @brief Initializes the RTC and assigns the tick hook function.
 *  @param rtcTickHook Tick hook function. The tick hook must have the function body shown in @ref rtc_tick_hook_t
 *  After initialization, the local date time variable is synchronized with the RTC
 *  @note This function must be called before any other function in this module can be used!
 */
void init_rtc(rtc_tick_hook_t rtcTickHook);

/*!
 *  @brief Synchronizes the local date time variable with the RTC.
 *  The new value is available through the rtc_get_date_time() function.
 *  @return true on success, false on failure
 */
bool rtc_sync(void);

/*!
 * @brief Return the local date time variable.
 * @note The timestamp is NOT updated automatically.
 * Call rtc_sync() before if the current timestamp is needed or use the rtc_get_unix_time()
 * function to get a constantly updated timestamp.
 */
rtc_date_time_t rtc_get_date_time(void);

/*!
 * @brief Returns the current date and time as unix timestamp.
 * @note This value is updated automatically.
 */
uint32_t rtc_get_unix_time(void);

/*!
 * @brief Returns the current uptime.
 */
uint32_t uptime_in_100_ms(void);

/*!
 * @brief Set the RTC.
 * @param dateTime Pointer to a variable of type @ref rtc_date_time_t with the values to set.
 * @return true on success, false on failure
 */
bool rtc_set_date_time(rtc_date_time_t *dateTime);

/*!
 * @brief Return the local date time as string.
 * @note The timestamp is NOT updated automatically.
 * Call rtc_sync() before if the current timestamp is needed.
 * @return Pointer to a string in the format YYYY-MM-DD_hh-mm-ss. The string is 26 bytes long.
 */
char* rtc_get_timestamp(void);


#endif /* RTC_H_ */
