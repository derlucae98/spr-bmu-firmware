#include "logger.h"

static TaskHandle_t _loggerTaskHandle = NULL;
static volatile bool _loggerActive = false;
static bool _terminateRequest = false;
static bool _terminated = false;
static void write_header(void);
static void logger_task(void *p);

typedef struct {
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
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
                    stacks_mutex_give();
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

    snprintf(buffer + offset, 3, "\r\n");

    counter++;
    return buffer;
}

static void write_header(void) {
    static const char *header = "Count;Cell 1;Cell 2;Cell 3;Cell 4;Cell 5;Cell 6;Cell 7;Cell 8;"
                                    "Cell 9;Cell 10;Cell 11;Cell 12;Cell 13;Cell 14;Cell 15;Cell 16;Cell 17;"
                                    "Cell 18;Cell 19;Cell 20;Cell 21;Cell 22;Cell 23;Cell 24;Cell 25;Cell 26;"
                                    "Cell 27;Cell 28;Cell 29;Cell 30;Cell 31;Cell 32;Cell 33;Cell 34;Cell 35;"
                                    "Cell 36;Cell 37;Cell 38;Cell 39;Cell 40;Cell 41;Cell 42;Cell 43;Cell 44;"
                                    "Cell 45;Cell 46;Cell 47;Cell 48;Cell 49;Cell 50;Cell 51;Cell 52;Cell 53;"
                                    "Cell 54;Cell 55;Cell 56;Cell 57;Cell 58;Cell 59;Cell 60;Cell 61;Cell 62;"
                                    "Cell 63;Cell 64;Cell 65;Cell 66;Cell 67;Cell 68;Cell 69;Cell 70;Cell 71;"
                                    "Cell 72;Cell 73;Cell 74;Cell 75;Cell 76;Cell 77;Cell 78;Cell 79;Cell 80;"
                                    "Cell 81;Cell 82;Cell 83;Cell 84;Cell 85;Cell 86;Cell 87;Cell 88;Cell 89;"
                                    "Cell 90;Cell 91;Cell 92;Cell 93;Cell 94;Cell 95;Cell 96;Cell 97;Cell 98;"
                                    "Cell 99;Cell 100;Cell 101;Cell 102;Cell 103;Cell 104;Cell 105;Cell 106;"
                                    "Cell 107;Cell 108;Cell 109;Cell 110;Cell 111;Cell 112;Cell 113;Cell 114;"
                                    "Cell 115;Cell 116;Cell 117;Cell 118;Cell 119;Cell 120;Cell 121;Cell 122;"
                                    "Cell 123;Cell 124;Cell 125;Cell 126;Cell 127;Cell 128;Cell 129;Cell 130;"
                                    "Cell 131;Cell 132;Cell 133;Cell 134;Cell 135;Cell 136;Cell 137;Cell 138;"
                                    "Cell 139;Cell 140;Cell 141;Cell 142;Cell 143;Cell 144;Min;Max;Avg\r\n";

    UINT bw;
    PRINTF("Writing header...\n");
    DSTATUS stat = f_write(_file, header, strlen(header), &bw);
    f_sync(_file);
    PRINTF("Status: %u, %u bytes written!\n", stat, bw);
}
