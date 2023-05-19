#include "logger.h"


static bool prvLoggerActive = true;
static bool _terminateRequest = false;
static bool _terminated = false;

static void prv_logger_prepare_task(void *p);
static void prv_logger_write_task(void *p);
static void prv_write_header(void);
static TaskHandle_t prvLoggerPrepareHandle = NULL;
static TaskHandle_t prvLoggerWriteHandle = NULL;
static QueueHandle_t prvLoggingQ = NULL;
static uint32_t prvUptime;
static bool prvSdInitialized = false;
static bool prvHeaderWritten = false;

#define NUMBER_OF_Q_ITEMS 10


typedef struct {
    uint32_t msgCnt; //Relative timestamp in 100 ms intervals
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint16_t temperature[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];
    float current;
    bool currentValid;
    float batteryVoltage;
    float dcLinkVoltage;
    bool voltageValid;
    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    bool cellVoltageValid;
    uint16_t minTemperature;
    uint16_t maxTemperature;
    uint16_t avgTemperature;
    bool cellTemperatureValid;
    uint32_t stateMachineError;
    uint8_t stateMachineState;
    uint16_t minSoc;
    uint16_t maxSoc;
    bool socValid;
    uint16_t crc16;
} __attribute__((packed)) logging_data_t;

typedef struct {
    char enc[LOGDATA_RAW_SIZE];
    size_t len;
} encoded_data_t;

static FIL *prvFile = NULL;


void logger_init(void) {
    prvLoggingQ = xQueueCreate(NUMBER_OF_Q_ITEMS, sizeof(encoded_data_t));
    configASSERT(prvLoggingQ);
    xTaskCreate(prv_logger_prepare_task, "logprep", 3000, NULL, 2, &prvLoggerPrepareHandle);
    xTaskCreate(prv_logger_write_task, "logwrite", 4000, NULL, 2, &prvLoggerWriteHandle);
}

void logger_control(bool ready, FIL *file) {
    prvSdInitialized = ready;
    if (file != prvFile) {
        //New file. Write CSV header.
        prvFile = file;
        prvHeaderWritten = false;
    }
}

bool logger_is_active(void) {
    return prvLoggerActive;
}

void logger_request_termination(void) {
    _terminateRequest = true;
}
bool logger_terminated(void) {
    return _terminated;
}

void logger_tick_hook(uint32_t uptime) {
    prvUptime = uptime;
    if (prvLoggerPrepareHandle) {
        xTaskNotifyGive(prvLoggerPrepareHandle);
    }
}

void prv_logger_prepare_task(void *p) {
    (void) p;
    logging_data_t loggingData;
    memset(&loggingData, 0, sizeof(logging_data_t));
    stacks_data_t stacksData;
    memset(&stacksData, 0, sizeof(stacks_data_t));
    adc_data_t adcData;
    memset(&adcData, 0, sizeof(adc_data_t));

    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            if (prvFile == NULL) {
                continue;
            }

            if (prvHeaderWritten) {
                if (prvLoggerActive && prvSdInitialized) {
                    copy_stacks_data(&stacksData, portMAX_DELAY);
                    copy_adc_data(&adcData, portMAX_DELAY);
                    static char buffer[300];
                    memset(buffer, 0xFF, sizeof(buffer)); //Clear buffer
                    uint16_t offset = 0;



                    snprintf(buffer, 8, "%06lu;", prvUptime);
                    offset += 7; // Length - 1, following string overrides termination character to build one long string

                    //Cell voltage 1 to 12
                    for (size_t cell = 0; cell < 12; cell++) {
                        snprintf(buffer + offset, 8, "%6.4f;", (float)(stacksData.cellVoltage[1][cell] * 0.0001f));
                        offset += 7;
                    }

                    //Current
                    snprintf(buffer + offset, 6, "%04.1f;", adcData.current);
                    offset += 5;

                    //Temperature 1 to 12
                    for (size_t sensor = 0; sensor < 6; sensor++) {
                        snprintf(buffer + offset, 6, "%04.1f;", (float)(stacksData.temperature[0][sensor] * 0.1f));
                        offset += 5;
                    }

                    //Temperature 13 to 24
                    for (size_t sensor = 0; sensor < 6; sensor++) {
                        snprintf(buffer + offset, 6, "%04.1f;", (float)(stacksData.temperature[1][sensor] * 0.1f));
                        offset += 5;
                    }

                    snprintf(buffer + offset - 1, 3, "\r\n");

                    encoded_data_t data;
                    strcpy(data.enc, buffer);
                    data.len = strlen(buffer) + 1;

                    xQueueSendToBack(prvLoggingQ, &data, portMAX_DELAY);

                }
            } else {
                prv_write_header();
                prvHeaderWritten = true;
            }

//            if (_terminateRequest) {
//                if (!sdInitPending) {
//                    f_close(prvFile);
//                }
//                _terminated = true;
//                prvLoggerActive = false;
//                vTaskSuspend(NULL);
//            }

        }
    }
}

void prv_logger_write_task(void *p) {
    (void) p;
    static encoded_data_t buf;
    static char enc[NUMBER_OF_Q_ITEMS * LOGDATA_RAW_SIZE];
    static uint32_t len = 0;
    static uint8_t index = 0;

    while (1) {
        if (xQueueReceive(prvLoggingQ, &buf, portMAX_DELAY)) {
            strcat(enc, &buf.enc[0]);
            len += buf.len;
            index++;
            if (index >= NUMBER_OF_Q_ITEMS) {
//                PRINTF("Writing data to file...\n");
                UINT bw;
                volatile TickType_t start = xTaskGetTickCount();
                f_write(prvFile, (void*)&enc[0], len, &bw);
                f_sync(prvFile);
                volatile TickType_t end = xTaskGetTickCount();
//                PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                index = 0;
                len = 0;
                memset(enc, 0, sizeof(enc));
            }
        }
    }
}

static void prv_write_header(void) {
    static const char *header = "time;Cell 1;Cell 2;Cell 3;Cell 4;Cell 5;Cell 6;Cell 7;Cell 8;"
                                        "Cell 9;Cell 10;Cell 11;Cell 12;Current;Temp 1;Temp 2;"
                                        "Temp 3;Temp 4;Temp 5;Temp 6;Temp 7;Temp 8;Temp 9;"
                                        "Temp 10;Temp 11;Temp 12\r\n";

        UINT bw;
        PRINTF("Writing header...\n");
        f_write(prvFile, header, strlen(header), &bw);
        f_sync(prvFile);
        PRINTF("Done!, %u bytes written!\n", bw);
}


