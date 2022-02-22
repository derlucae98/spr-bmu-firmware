#include "rtc.h"

#define CONTROL_1           0x00
#define CONTROL_INT         0x01
#define CONTROL_INT_FLAG    0x02
#define CONTROL_STATUS      0x03
#define CONTROL_RESET       0x04

#define CLOCK_SECONDS       0x08
#define CLOCK_MINUTES       0x09
#define CLOCK_HOURS         0x0A
#define CLOCK_DAYS          0x0B
#define CLOCK_WEEKDAYS      0x0C
#define CLOCK_MONTHS        0x0D
#define CLOCK_YEARS         0x0E

#define ALARM_SECOND        0x10
#define ALARM_MINUTE        0x11
#define ALARM_HOUR          0x12
#define ALARM_DAYS          0x13
#define ALARM_WEEKDAYS      0x14
#define ALARM_MONTHS        0x15
#define ALARM_YEARS         0x16

#define TIMER_LOW           0x18
#define TIMER_HIGH          0x19




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
rtc_date_time_t rtcDateTime; //variable secured by mutex

extern void PRINTF(const char *format, ...);

static rtc_tick_hook_t _tickHook = NULL;
static SemaphoreHandle_t _dateTimeMutex = NULL;

static inline void assert_cs(void);
static inline void deassert_cs(void);
static void rtc_irq_handler(BaseType_t *higherPrioTaskWoken);

void rtc_register_tick_hook(rtc_tick_hook_t rtcTickHook) {
    _tickHook = rtcTickHook;
}

void rtc_init(void) {
    _dateTimeMutex = xSemaphoreCreateMutex();
    configASSERT(_dateTimeMutex);

    attach_interrupt(INT_RTC_PORT, INT_RTC_PIN, IRQ_EDGE_FALLING, rtc_irq_handler);
    uint8_t send[4];
    send[0] = COMMAND_WRITE(CONTROL_1);
    send[1] = 0x98; //Interrupt every second
    send[2] = 0; //Clear all interrupts
    send[3] = 0; //Clear all interrupts
    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
        assert_cs();
        spi_move_array(RTC_SPI, send, 4);
        deassert_cs();
        spi_mutex_give(RTC_SPI);
    }
    xTaskCreate(rtc_tick_task, "rtc tick", 300, NULL, 3, &_rtcTickTaskHandle);
}

BaseType_t rtc_date_time_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_dateTimeMutex, blocktime);
}

void rtc_date_time_mutex_give(void) {
    xSemaphoreGive(_dateTimeMutex);
}

void rtc_set_date_time(rtc_date_time_t *dateTime) {
//    uint8_t sendBuffer[8];
//    sendBuffer[0] = COMMAND_WRITE(REGISTER_SECONDS);
//    sendBuffer[1] = DECTOSEC(dateTime->second);
//    sendBuffer[2] = DECTOMIN(dateTime->minute);
//    sendBuffer[3] = DECTOHOUR(dateTime->hour);
//    sendBuffer[4] = DECTODAY(dateTime->day);
//    sendBuffer[5] = 0; //Weekday
//    sendBuffer[6] = DECTOMON(dateTime->month);
//    sendBuffer[7] = DECTOYEAR((dateTime->year - 2000));
//
//    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
//        assert_cs();
//        spi_move_array(RTC_SPI, sendBuffer, 8);
//        deassert_cs();
//        spi_mutex_give(RTC_SPI);
//    }
}

char* rtc_get_timestamp(TickType_t blocktime) {
    static char timestamp[20];
    if (rtc_date_time_mutex_take(blocktime)) {
        snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u", rtcDateTime.year,
                rtcDateTime.month, rtcDateTime.day, rtcDateTime.hour, rtcDateTime.minute, rtcDateTime.second);
        rtc_date_time_mutex_give();
    }
    return timestamp;
}

void rtc_tick_task(void *p) {
//    (void)p;
//    uint32_t notification;
//    uint8_t cmdBuffer[15];
//    uint8_t recBuffer[15];
//    memset(cmdBuffer, 0, sizeof(cmdBuffer)/sizeof(uint8_t));
//    memset(recBuffer, 0, sizeof(recBuffer)/sizeof(uint8_t));
//    while (1) {
//        notification = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000));
//        if (notification > 0) {
//
//            if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
//                //Check, which interrupt occurred
//                cmdBuffer[0] = COMMAND_READ(REGISTER_CONTROL_2);
//                assert_cs();
//                spi_move_array(RTC_SPI, cmdBuffer, 2);
//                deassert_cs();
//
//                if (cmdBuffer[1] & 0x80) {
//                    //second interrupt generated
//                    rtc_sync();
//                }
//
//                //Clear interrupt
//                cmdBuffer[0] = COMMAND_WRITE(REGISTER_CONTROL_2);
//                cmdBuffer[1] = 0; //Clear all interrupts
//                cmdBuffer[2] = 0; //Clear all interrupts
//                assert_cs();
//                spi_move_array(RTC_SPI, cmdBuffer, 3);
//                deassert_cs();
//
//                if (recBuffer[1] & 0x80) {
//                    //OSF flag set, clear it
//                    cmdBuffer[0] = COMMAND_WRITE(REGISTER_SECONDS);
//                    cmdBuffer[1] = 0x80;
//                    assert_cs();
//                    spi_move_array(RTC_SPI, cmdBuffer, 2);
//                    deassert_cs();
//                }
//                spi_mutex_give(RTC_SPI);
//            }
//
//            if (_tickHook != NULL) {
//                (_tickHook)(); //Invoke callback function
//            }
//        }
//    }
}

void rtc_sync(void) {
//    uint8_t recBuffer[8];
//    if (spi_mutex_take(RTC_SPI, pdMS_TO_TICKS(100))) {
//        recBuffer[0] = COMMAND_READ(REGISTER_SECONDS);
//        assert_cs();
//        spi_move_array(RTC_SPI, recBuffer, 8);
//        deassert_cs();
//        spi_mutex_give(RTC_SPI);
//        if (rtc_date_time_mutex_take(pdMS_TO_TICKS(100))) {
//            rtcDateTime.second = SECTODEC(recBuffer[1]);
//            rtcDateTime.minute = MINTODEC(recBuffer[2]);
//            rtcDateTime.hour = HOURTODEC(recBuffer[3]);
//            rtcDateTime.day = DAYTODEC(recBuffer[4]);
//            rtcDateTime.month = MONTODEC(recBuffer[6]);
//            rtcDateTime.year = YEARTODEC(recBuffer[7]) + 2000;
//            rtc_date_time_mutex_give();
//        }
//    }
}

static void rtc_irq_handler(BaseType_t *higherPrioTaskWoken) {
    if (_rtcTickTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(_rtcTickTaskHandle, higherPrioTaskWoken);
    }
}

static inline void assert_cs(void) {
    clear_pin(CS_RTC_PORT, CS_RTC_PIN);
}

static inline void deassert_cs(void) {
    set_pin(CS_RTC_PORT, CS_RTC_PIN);
}
