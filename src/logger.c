#include "logger.h"

static TaskHandle_t _loggerTaskHandle = NULL;
static volatile bool _loggerActive = false;
static bool _terminateRequest = false;
static bool _terminated = false;
static void logger_task(void *p);

typedef struct {
    rtc_date_time_t timestamp;
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint16_t temperature[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];
    float current;
    float batteryVoltage;
    float dcLinkVoltage;
    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    uint16_t minTemperature;
    uint16_t maxTemperature;
    uint16_t avgTemperature;
    uint8_t stateMachineError;
    uint8_t stateMachineState;
    uint16_t minSoc;
    uint16_t maxSoc;
} __attribute__((packed)) logging_data_t;

static FIL *_file = NULL;

extern void PRINTF(const char *format, ...);

void logger_init(void) {
    xTaskCreate(logger_task, "logger", 700, NULL, 2, &_loggerTaskHandle);
}

void logger_start(void) {
    _loggerActive = true;
}

void logger_stop(void) {
    _loggerActive = false;
}

void logger_set_file(FIL *file) {
    _file = file;
}

bool logger_is_active(void) {
    return _loggerActive;
}

void logger_request_termination(void) {
    _terminateRequest = true;
}
bool logger_terminated(void) {
    return _terminated;
}

void logger_tick_hook(void) {
    if (_loggerTaskHandle) {
        xTaskNotifyGive(_loggerTaskHandle);
    }
}

void logger_task(void *p) {
    (void) p;
    logging_data_t loggingData;
    memset(&loggingData, 0, sizeof(logging_data_t));


    while (1) {
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000))) {

            if (_loggerActive) {

                //Daten zusammenkopieren

                size_t len;
                char *buf = base64_encode((unsigned char*)&loggingData, sizeof(loggingData), &len);

                if (!sdInitPending) {
                    UINT bw;
                    volatile TickType_t start = xTaskGetTickCount();
                    f_write(_file, buf, len, &bw);
                    f_sync(_file);
                    volatile TickType_t end = xTaskGetTickCount();
                    PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                }
            }
            if (_terminateRequest) {
                if (!sdInitPending) {
                    f_close(_file);
                }
                _terminated = true;
                _loggerActive = false;
                vTaskSuspend(NULL);
            }

        }
    }
}

