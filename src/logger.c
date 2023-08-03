#include "logger.h"


static void prv_logger_prepare_task(void *p);
static void prv_logger_write_task(void *p);
static TaskHandle_t prvLoggerPrepareHandle = NULL;
static TaskHandle_t prvLoggerWriteHandle = NULL;
static QueueHandle_t prvLoggingQ = NULL;
static uint32_t prvUptime;
static bool prvSdInitialized = false;
#define NUMBER_OF_Q_ITEMS 10
static FIL *prvFile = NULL;


void logger_init(void) {
    prvLoggingQ = xQueueCreate(NUMBER_OF_Q_ITEMS, sizeof(log_data_t));
    configASSERT(prvLoggingQ);
    xTaskCreate(prv_logger_prepare_task, "logprep", 3000, NULL, 2, &prvLoggerPrepareHandle);
    xTaskCreate(prv_logger_write_task, "logwrite", 4000, NULL, 2, &prvLoggerWriteHandle);
}

void logger_control(bool ready, FIL *file) {
    prvSdInitialized = ready;
    prvFile = file;
}

void logger_tick_hook(uint32_t uptime) {
    prvUptime = uptime;
    if (prvLoggerPrepareHandle) {
        xTaskNotifyGive(prvLoggerPrepareHandle);
    }
}

void prv_logger_prepare_task(void *p) {
    (void) p;
    log_data_t loggingData;

    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {

            if (prvFile == NULL) {
                continue;
            }

            if (0/*prvSdInitialized*/) {

                //TODO: CSV logger
                xQueueSendToBack(prvLoggingQ, &loggingData, portMAX_DELAY);

            }
        }
    }
}

void prv_logger_write_task(void *p) {
    (void) p;
    uint8_t index = 0;
    static log_data_t element;
    static log_data_t buf[NUMBER_OF_Q_ITEMS];

    while (1) {
        if (xQueueReceive(prvLoggingQ, &element, portMAX_DELAY)) {
            buf[index] = element;
            index++;
            if (index >= NUMBER_OF_Q_ITEMS) {
                PRINTF("Writing data to file...\n");
                UINT bw;
                volatile TickType_t start = xTaskGetTickCount();
                f_write(prvFile, (void*)&buf, NUMBER_OF_Q_ITEMS * sizeof(log_data_t), &bw);
                f_sync(prvFile);
                volatile TickType_t end = xTaskGetTickCount();
                PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                index = 0;
            }
        }
    }
}


