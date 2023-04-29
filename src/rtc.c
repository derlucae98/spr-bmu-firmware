/*!
 * @file            rtc.c
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

#include "rtc.h"

#define CONTROL_1               (0x00)
#define CONTROL_1_CLK_INT       (1 << 7)
#define CONTROL_1_TD_32         (0 << 5)
#define CONTROL_1_TD_8          (1 << 5)
#define CONTROL_1_TD_1          (2 << 5)
#define CONTROL_1_TD_05         (3 << 5)
#define CONTROL_1_SROn          (1 << 4)
#define CONTROL_1_EERE          (1 << 3)
#define CONTROL_1_TAR           (1 << 2)
#define CONTROL_1_TE            (1 << 1)
#define CONTROL_1_WE            (1 << 0)

#define CONTROL_INT             (0x01)
#define CONTROL_INT_SRIE        (1 << 4)
#define CONTROL_INT_V2IE        (1 << 3)
#define CONTROL_INT_V1IE        (1 << 2)
#define CONTROL_INT_TIE         (1 << 1)
#define CONTROL_INT_AIE         (1 << 0)

#define CONTROL_INT_FLAG        (0x02)
#define CONTROL_INT_FLAG_SRF    (1 << 4)
#define CONTROL_INT_FLAG_V2IF   (1 << 3)
#define CONTROL_INT_FLAG_V1IF   (1 << 2)
#define CONTROL_INT_FLAG_TF     (1 << 1)
#define CONTROL_INT_FLAG_AF     (1 << 0)

#define CONTROL_STATUS          (0x03)
#define CONTROL_STATUS_EEbusy   (1 << 7)
#define CONTROL_STATUS_PON      (1 << 5)
#define CONTROL_STATUS_SR       (1 << 4)
#define CONTROL_STATUS_V2F      (1 << 3)
#define CONTROL_STATUS_V1F      (1 << 2)

#define CONTROL_EEPROM          (0x30)
#define CONTROL_EEPROM_R80k     (1 << 7)
#define CONTROL_EEPROM_R20k     (1 << 6)
#define CONTROL_EEPROM_R5k      (1 << 5)
#define CONTROL_EEPROM_R1k      (1 << 4)
#define CONTROL_EEPROM_FD0      (1 << 3)
#define CONTROL_EEPROM_FD1      (1 << 2)
#define CONTROL_EEPROM_ThE      (1 << 1)
#define CONTROL_EEPROM_ThP      (1 << 0)

#define CONTROL_RESET           (0x04)
#define CONTROL_RESET_SysR      (1 << 4)

#define CLOCK_SECONDS           (0x08)
#define CLOCK_MINUTES           (0x09)
#define CLOCK_HOURS             (0x0A)
#define CLOCK_DAYS              (0x0B)
#define CLOCK_WEEKDAYS          (0x0C)
#define CLOCK_MONTHS            (0x0D)
#define CLOCK_YEARS             (0x0E)
#define CLOCK_12_HOUR_MODE      (1 << 6)
#define CLOCK_AM_PM             (1 << 5)

#define ALARM_SECOND            (0x10)
#define ALARM_MINUTE            (0x11)
#define ALARM_HOUR              (0x12)
#define ALARM_DAYS              (0x13)
#define ALARM_WEEKDAYS          (0x14)
#define ALARM_MONTHS            (0x15)
#define ALARM_YEARS             (0x16)
#define ALARM_AE                (1 << 7)

#define TIMER_LOW               (0x18)
#define TIMER_HIGH              (0x19)


#define COMMAND_READ(x) (0x80 | (x)) //x = command
#define COMMAND_WRITE(x) (x) //x = command

#define SECTODEC(x)  ( ( (x & 0x70) >> 4 ) * 10 + (x & 0x0F) ) //Convert BCD coded second to uint8
#define MINTODEC(x) (SECTODEC(x)) //Convert BCD coded minute to uint8
#define HOURTODEC(x) ( ( (x & 0x30) >> 4)  * 10 + (x & 0x0F) ) //Convert BCD coded hour to uint8
#define DAYTODEC(x) (HOURTODEC(x)) //Convert BCD coded day to uint8
#define MONTODEC(x)  ( ( (x & 0x10) >> 4) * 10 +  (x & 0x0F) ) //Convert BCD coded month to uint8
#define YEARTODEC(x) ( ( (x & 0xF0) >> 4) * 10 +  (x & 0x0F) ) //Convert BCD coded year to uint8

#define DECTOSEC(x) ((((x / 10) & 0x7) << 4) | ((x % 10) & 0xF))
#define DECTOMIN(x) (DECTOSEC(x))
#define DECTOHOUR(x) ((((x / 10) & 0x3) << 4) | ((x % 10) & 0xF))
#define DECTODAY(x) (DECTOHOUR(x))
#define DECTOMON(x) ((((x / 10) & 0x1) << 4) | ((x % 10) & 0xF))
#define DECTOYEAR(x) ((((x / 10) & 0xF) << 4) | ((x % 10) & 0xF))


static rtc_date_time_t prvRtcDateTime;
static rtc_tick_hook_t prvTickHook = NULL;
static uint32_t prvUptime = 0;
static uint32_t prvEpoch = 0;

static void prv_rtc_clkout_handler(BaseType_t *higherPrioTaskWoken);
static uint32_t prv_make_unix_time(void);

static TimerHandle_t prvTimer;
static void prv_timer_callback(TimerHandle_t xTimer);

static inline void prv_assert_cs(void) {
    set_pin(CS_RTC_PORT, CS_RTC_PIN);
}

static inline void prv_deassert_cs(void) {
    clear_pin(CS_RTC_PORT, CS_RTC_PIN);
}

void init_rtc(rtc_tick_hook_t rtcTickHook) {

    prvTickHook = rtcTickHook;

    prvTimer = xTimerCreate("100ms", pdMS_TO_TICKS(100), pdTRUE, NULL, prv_timer_callback);

    //Initialize RTC
    uint8_t init[5];
    init[0] = COMMAND_WRITE(CONTROL_1); //Control page
    init[1] = CONTROL_1_CLK_INT | CONTROL_1_TD_8 | CONTROL_1_SROn | CONTROL_1_EERE | CONTROL_1_WE; //Control_1
    init[2] = CONTROL_INT_SRIE; //Control_INT
    init[3] = 0; //Control_INT Flag
    init[4] = 0; //Control_Status

    //Enable 1 Hz Clkout
    uint8_t clkout[2];
    clkout[0] = COMMAND_WRITE(CONTROL_EEPROM);
    clkout[1] = CONTROL_EEPROM_FD1 | CONTROL_EEPROM_FD0;

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(500))) {
        prv_assert_cs();
        spi_move_array(RTC_SPI, clkout, sizeof(clkout));
        prv_deassert_cs();

        prv_assert_cs();
        spi_move_array(RTC_SPI, init, sizeof(init));
        prv_deassert_cs();
        spi_mutex_give(RTC_SPI);
    } else {
        PRINTF("RTC: Initialization failed! Could not get SPI mutex!\n");
        configASSERT(0);
    }

    rtc_sync();

    prvEpoch = prv_make_unix_time();

    attach_interrupt(CLKOUT_RTC_PORT, CLKOUT_RTC_PIN, IRQ_EDGE_FALLING, prv_rtc_clkout_handler);
}

bool rtc_sync(void) {
    uint8_t time[8];
    memset(time, 0xFF, sizeof(time));
    time[0] = COMMAND_READ(CLOCK_SECONDS);

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
        prv_assert_cs();
        spi_move_array(RTC_SPI, time, sizeof(time));
        prv_deassert_cs();
        spi_mutex_give(RTC_SPI);

        prvRtcDateTime.second = SECTODEC(time[1]);
        prvRtcDateTime.minute = MINTODEC(time[2]);
        prvRtcDateTime.hour   = HOURTODEC(time[3]);
        prvRtcDateTime.day    = DAYTODEC(time[4]);
        prvRtcDateTime.month  = MONTODEC(time[6]);
        prvRtcDateTime.year   = YEARTODEC(time[7]) + 2000;
    } else {
        return false;
    }

    return true;
}

rtc_date_time_t rtc_get_date_time(void) {
    return prvRtcDateTime;
}

uint32_t rtc_get_unix_time(void) {
    return prvEpoch;
}

uint32_t uptime_in_100_ms(void) {
    return prvUptime;
}

bool rtc_set_date_time(rtc_date_time_t *dateTime) {
    uint8_t buffer[8];
    buffer[0] = COMMAND_WRITE(CLOCK_SECONDS);
    buffer[1] = DECTOSEC(dateTime->second);
    buffer[2] = DECTOMIN(dateTime->minute);
    buffer[3] = DECTOHOUR(dateTime->hour);
    buffer[4] = DECTODAY(dateTime->day);
    buffer[5] = 0; //Weekday
    buffer[6] = DECTOMON(dateTime->month);
    buffer[7] = DECTOYEAR((dateTime->year - 2000));

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(1000))) {
        prv_assert_cs();
        spi_move_array(RTC_SPI, buffer, sizeof(buffer));
        prv_deassert_cs();
        spi_mutex_give(RTC_SPI);
    } else {
        return false;
    }

    return true;
}

char* rtc_get_timestamp(void) {
    static char timestamp[26];
    snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u", prvRtcDateTime.year,
            prvRtcDateTime.month, prvRtcDateTime.day, prvRtcDateTime.hour, prvRtcDateTime.minute, prvRtcDateTime.second);
    return timestamp;
}

static uint32_t prv_make_unix_time(void) {
    struct tm t;
    time_t epoch;

    t.tm_year = prvRtcDateTime.year-1900;
    t.tm_mon = prvRtcDateTime.month - 1;
    t.tm_mday = prvRtcDateTime.day;
    t.tm_hour = prvRtcDateTime.hour;
    t.tm_min = prvRtcDateTime.minute;
    t.tm_sec = prvRtcDateTime.second;
    t.tm_isdst = -1;
    epoch = mktime(&t);
    return (uint32_t) epoch;
}

static void prv_rtc_clkout_handler(BaseType_t *higherPrioTaskWoken) {
    //Restart the software timer to sync it with the RTC
    xTimerResetFromISR(prvTimer, higherPrioTaskWoken);
    prvEpoch++;
}

static void prv_timer_callback(TimerHandle_t timer) {
    (void) timer;
    prvUptime++;
    if (prvTickHook != NULL) {
        (prvTickHook)(prvUptime); //Invoke callback function
    }
}

