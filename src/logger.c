#include "logger.h"

static TaskHandle_t _loggerTaskHandle = NULL;
static volatile bool _loggerActive = false;
static bool _terminateRequest = false;
static bool _terminated = false;
static void write_header(void);
static void logger_task(void *p);

typedef struct {
    uint16_t cellVoltage[NUMBEROFSLAVES][MAXCELLS];
    uint8_t cellVoltageStatus[NUMBEROFSLAVES][MAXCELLS+1];
    uint16_t minCellVolt;
    bool minCellVoltValid;
    uint16_t maxCellVolt;
    bool maxCellVoltValid;
    uint16_t avgCellVolt;
    bool avgCellVoltValid;
    uint16_t minTemperature;
    bool minTemperatureValid;
    uint16_t maxTemperature;
    bool maxTemperatureValid;
    uint16_t avgTemperature;
    bool avgTemperatureValid;
    uint16_t current;
    uint16_t temperature[NUMBEROFSLAVES][MAXTEMPSENS];
} logging_data_t;


static char* prepare_data(rtc_date_time_t *dateTime, logging_data_t *loggingData);

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
    write_header();
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
    char *buffer;
    rtc_date_time_t dateTime;
    logging_data_t loggingData;
    memset(&loggingData, 0, sizeof(logging_data_t));
    memset(&dateTime, 0, sizeof(rtc_date_time_t));

    while (1) {
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000))) {
            dbg4_set();
            if (_loggerActive) {
                //Get timestamp
//                if (rtc_date_time_mutex_take(pdMS_TO_TICKS(50))) {
//                    memcpy(&dateTime, &rtcDateTime, sizeof(rtc_date_time_t));
//                    rtc_date_time_mutex_give();
//                }
                //Get battery Data
                if (stacks_mutex_take(pdMS_TO_TICKS(500))) {
                    memcpy(loggingData.cellVoltage, stacksData.cellVoltage, sizeof(stacksData.cellVoltage));
                    memcpy(loggingData.cellVoltageStatus, stacksData.cellVoltageStatus, sizeof(stacksData.cellVoltageStatus));

                    loggingData.minCellVolt = stacksData.minCellVolt;
                    loggingData.maxCellVolt = stacksData.maxCellVolt;
                    loggingData.avgCellVolt = stacksData.avgCellVolt;
                    memcpy(loggingData.temperature, stacksData.temperature, sizeof(stacksData.temperature));
                    stacks_mutex_give();
                }
                if (sensor_mutex_take(pdMS_TO_TICKS(50))) {
                    loggingData.current = sensorData.current;
                    sensor_mutex_give();
                }
                buffer = prepare_data(&dateTime, &loggingData);
                if (!sdInitPending) {
                    UINT bw;
                    f_write(_file, buffer, strlen(buffer), &bw);
                    f_sync(_file);
                    //PRINTF("Logger: %lu bytes written!\n", bw);
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
            dbg4_clear();
        }
    }
}

char* prepare_data(rtc_date_time_t *dateTime, logging_data_t *loggingData) {
    static char buffer[2500]; // Large enough to hold all values
    memset(buffer, 0xFF, sizeof(buffer)); //Clear buffer

    uint16_t offset = 0;

    //Counter
    static uint32_t counter;

    snprintf(buffer, 8, "%6lu;", counter);
    offset += 7; // Length - 1, following string overrides termination character to build one long string


    //Cell voltage 1 to 144
    for (size_t cell = 0; cell < NUMBEROFSLAVES * MAXCELLS; cell++) {
        snprintf(buffer + offset, 7, "%5.3f;", (float)(loggingData->cellVoltage[cell / 12][cell % 12] * 0.001f));
        offset += 6;
    }

    //Min, Max, Avg cell volt
    snprintf(buffer + offset, 7, "%5.3f;", (float)(loggingData->minCellVolt * 0.001f));
    offset += 6;
    snprintf(buffer + offset, 7, "%5.3f;", (float)(loggingData->maxCellVolt * 0.001f));
    offset += 6;
    snprintf(buffer + offset, 7, "%5.3f;", (float)(loggingData->avgCellVolt * 0.001f));
    offset += 6;
    snprintf(buffer + offset, 8, "%6.2f;", (float)(loggingData->current * 0.01f));
    offset += 7;

    //Cell temperature
    for (size_t cell = 0; cell < NUMBEROFSLAVES * MAXTEMPSENS; cell++) {
        snprintf(buffer + offset, 6, "%4.1f;", (float)(loggingData->temperature[cell / 12][cell % 12] * 0.1f));
        offset += 5;
    }
    snprintf(buffer + offset, 3, "\r\n");

    counter++;
    return buffer;
}

static void write_header(void) {
    static const char *header = "Count;Cell 1;Cell 2;Cell 3;Cell 4;Cell 5;Cell 6;Cell 7;Cell 8;"
                                    "Cell 9;Cell 10;Cell 11;Cell 12;Min;Max;Avg;curr;Temp 1;Temp 2;Temp 3;Temp 4;Temp 5;Temp 6;Temp 7;Temp 8;"
                                    "Temp 9;Temp 10;Temp 11;Temp 12\r\n";

    UINT bw;
    PRINTF("Writing header...\n");
    DSTATUS stat = f_write(_file, header, strlen(header), &bw);
    f_sync(_file);
    PRINTF("Status: %u, %u bytes written!\n", stat, bw);
}
