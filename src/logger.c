#include "logger.h"


static bool prvLoggerActive = true;
static bool _terminateRequest = false;
static bool _terminated = false;

static void prv_logger_prepare_task(void *p);
static void prv_logger_write_task(void *p);
static TaskHandle_t prvLoggerPrepareHandle = NULL;
static TaskHandle_t prvLoggerWriteHandle = NULL;
static QueueHandle_t prvLoggingQ = NULL;
static uint32_t prvUptime;
static bool prvSdInitialized = false;

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
    xTaskCreate(prv_logger_prepare_task, "logprep", 2000, NULL, 2, &prvLoggerPrepareHandle);
    xTaskCreate(prv_logger_write_task, "logwrite", 4000, NULL, 2, &prvLoggerWriteHandle);
}

void logger_control(bool ready, FIL *file) {
    prvSdInitialized = ready;
    prvFile = file;
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


    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {

            if (prvLoggerActive && prvSdInitialized) {

                //Daten zusammenkopieren
                size_t len;
                char *enc = base64_encode((unsigned char*)&loggingData, sizeof(loggingData), &len);
                encoded_data_t data;
                strcpy(data.enc, enc);
                data.len = len;

                xQueueSendToBack(prvLoggingQ, &data, portMAX_DELAY);

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
                PRINTF("Writing data to file...\n");
                UINT bw;
                volatile TickType_t start = xTaskGetTickCount();
                f_write(prvFile, (void*)&enc[0], len, &bw);
                f_sync(prvFile);
                volatile TickType_t end = xTaskGetTickCount();
                PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                index = 0;
                len = 0;
                memset(enc, 0, sizeof(enc));
            }
        }
    }
}


