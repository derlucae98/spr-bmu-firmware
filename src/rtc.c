#include "rtc.h"

/* RTC: Microcrystal RV-3149-C3
 * https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-3149-C3_App-Manual.pdf
 */

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

static TaskHandle_t _rtcTickTaskHandle = NULL;
static rtc_date_time_t _rtcDateTime;
static rtc_tick_hook_t _tickHook = NULL;
static SemaphoreHandle_t _dateTimeMutex = NULL;

extern void PRINTF(const char *format, ...);
static inline void assert_cs(void);
static inline void deassert_cs(void);
static void rtc_irq_handler(BaseType_t *higherPrioTaskWoken);
static BaseType_t rtc_date_time_mutex_take(TickType_t blocktime);

void rtc_register_tick_hook(rtc_tick_hook_t rtcTickHook) {
    _tickHook = rtcTickHook;
}

void init_rtc(void) {
    _dateTimeMutex = xSemaphoreCreateMutex();
    configASSERT(_dateTimeMutex);

    attach_interrupt(IRQ_RTC_PORT, IRQ_RTC_PIN, IRQ_EDGE_FALLING, rtc_irq_handler);
    xTaskCreate(rtc_tick_task, "rtc tick", 300, NULL, 3, &_rtcTickTaskHandle);

    uint8_t init[5];
    init[0] = COMMAND_WRITE(CONTROL_1);
    init[1] = CONTROL_1_TD_8 | CONTROL_1_SROn | CONTROL_1_EERE | CONTROL_1_WE | CONTROL_1_TE | CONTROL_1_TAR;
    init[2] = CONTROL_INT_SRIE | CONTROL_INT_TIE;
    init[3] = 0;
    init[4] = 0;

    uint8_t timer[3];
    timer[0] = COMMAND_WRITE(TIMER_LOW);
    timer[1] = 7; //Countdown timer interrupts every second
    timer[2] = 0;

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(500))) {
        assert_cs();
        spi_move_array(RTC_SPI, timer, sizeof(timer));
        deassert_cs();

        assert_cs();
        spi_move_array(RTC_SPI, init, sizeof(init));
        deassert_cs();
        spi_mutex_give(RTC_SPI);
    }

    rtc_sync();
}

void rtc_print_time(void) {
    uint8_t time[8];
    memset(time, 0xFF, sizeof(time));
    time[0] = COMMAND_READ(CLOCK_SECONDS);
    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
        assert_cs();
        spi_move_array(RTC_SPI, time, sizeof(time));
        deassert_cs();
        spi_mutex_give(RTC_SPI);
        PRINTF("%02u.%02u.%04u %02u:%02u:%02u\n", DAYTODEC(time[4]), MONTODEC(time[6]), YEARTODEC(time[7]) + 2000, HOURTODEC(time[3]), MINTODEC(time[2]), SECTODEC(time[1]));
    }
}

BaseType_t rtc_date_time_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_dateTimeMutex, blocktime);
}

void release_rtc_date_time(void) {
    xSemaphoreGive(_dateTimeMutex);
}

rtc_date_time_t* get_rtc_date_time(TickType_t blocktime) {
    if (rtc_date_time_mutex_take(blocktime)) {
        return &_rtcDateTime;
    } else {
        return NULL;
    }
}

bool copy_rtc_date_time(rtc_date_time_t *dest, TickType_t blocktime) {
    rtc_date_time_t *src = get_rtc_date_time(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(rtc_date_time_t));
        release_rtc_date_time();
        return true;
    } else {
        return false;
    }
}

void rtc_set_date_time(rtc_date_time_t *dateTime) {
    uint8_t buffer[8];
    buffer[0] = COMMAND_WRITE(CLOCK_SECONDS);
    buffer[1] = DECTOSEC(dateTime->second);
    buffer[2] = DECTOMIN(dateTime->minute);
    buffer[3] = DECTOHOUR(dateTime->hour);
    buffer[4] = DECTODAY(dateTime->day);
    buffer[5] = 0; //Weekday
    buffer[6] = DECTOMON(dateTime->month);
    buffer[7] = DECTOYEAR((dateTime->year - 2000));

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
        assert_cs();
        spi_move_array(RTC_SPI, buffer, sizeof(buffer));
        deassert_cs();
        spi_mutex_give(RTC_SPI);
    }
}

char* rtc_get_timestamp(TickType_t blocktime) {
    static char timestamp[20];
    rtc_date_time_t *dateTime = get_rtc_date_time(blocktime);
    if (dateTime != NULL) {
        snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u", dateTime->year,
                dateTime->month, dateTime->day, dateTime->hour, dateTime->minute, dateTime->second);
        release_rtc_date_time();
    }

    return timestamp;
}

void rtc_tick_task(void *p) {
    (void)p;
    uint32_t notification;
    while (1) {
        notification = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000));
        if (notification > 0) {
            rtc_sync();

            uint8_t buffer[2];
            buffer[0] = COMMAND_WRITE(CONTROL_INT_FLAG);
            buffer[1] = 0;
            //Clear all interrupt flags
            if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
                assert_cs();
                spi_move_array(RTC_SPI, buffer, sizeof(buffer));
                deassert_cs();
                spi_mutex_give(RTC_SPI);
            }

            if (_tickHook != NULL) {
                (_tickHook)(); //Invoke callback function
            }
        }
    }
}

void rtc_sync(void) {
    uint8_t time[8];
    memset(time, 0xFF, sizeof(time));
    time[0] = COMMAND_READ(CLOCK_SECONDS);

    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
        assert_cs();
        spi_move_array(RTC_SPI, time, sizeof(time));
        deassert_cs();
        spi_mutex_give(RTC_SPI);
        rtc_date_time_t *dateTime = get_rtc_date_time(pdMS_TO_TICKS(100));
        if (dateTime != NULL) {
            dateTime->second = SECTODEC(time[1]);
            dateTime->minute = MINTODEC(time[2]);
            dateTime->hour = HOURTODEC(time[3]);
            dateTime->day = DAYTODEC(time[4]);
            dateTime->month = MONTODEC(time[6]);
            dateTime->year = YEARTODEC(time[7]) + 2000;
            release_rtc_date_time();
        }
    }
}

static void rtc_irq_handler(BaseType_t *higherPrioTaskWoken) {
    if (_rtcTickTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(_rtcTickTaskHandle, higherPrioTaskWoken);
    }
}

static inline void assert_cs(void) {
    set_pin(CS_RTC_PORT, CS_RTC_PIN);
}

static inline void deassert_cs(void) {
    clear_pin(CS_RTC_PORT, CS_RTC_PIN);
}
