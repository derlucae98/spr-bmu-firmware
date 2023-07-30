#include "logger.h"


static bool prvLoggerActive = true;

static void prv_logger_prepare_task(void *p);
static void prv_logger_write_task(void *p);
static TaskHandle_t prvLoggerPrepareHandle = NULL;
static TaskHandle_t prvLoggerWriteHandle = NULL;
static QueueHandle_t prvLoggingQ = NULL;
static uint32_t prvUptime;
static bool prvSdInitialized = false;


#define NUMBER_OF_Q_ITEMS 10

#define START_TOKEN 0xF5A5

typedef struct {
    uint16_t start; //Token to recognize the start of a block of data. Value does not match a valid cell voltage to be distinguishable from following bytes.
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
    bool temperatureValid;
    uint32_t stateMachineError;
    uint8_t stateMachineState;
    uint16_t minSoc;
    uint16_t maxSoc;
    bool socValid;
    uint16_t isoResistance;
    bool isoResistanceValid;
    uint16_t crc16;
} __attribute__((packed)) logging_data_t; //481 bytes


static FIL *prvFile = NULL;


void logger_init(void) {
    prvLoggingQ = xQueueCreate(NUMBER_OF_Q_ITEMS, sizeof(logging_data_t));
    configASSERT(prvLoggingQ);
    config_t* config  = get_config(pdMS_TO_TICKS(500));
    if (config != NULL) {
        prvLoggerActive = config->loggerEnable;
        release_config();
    }
    xTaskCreate(prv_logger_prepare_task, "logprep", 3000, NULL, 2, &prvLoggerPrepareHandle);
    xTaskCreate(prv_logger_write_task, "logwrite", 4000, NULL, 2, &prvLoggerWriteHandle);
}

void logger_control(bool ready, FIL *file) {
    prvSdInitialized = ready;
    prvFile = file;
}

bool logger_is_active(void) {
    return prvLoggerActive;
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

            if (prvLoggerActive && prvSdInitialized) {
                copy_stacks_data(&stacksData, portMAX_DELAY);
                copy_adc_data(&adcData, portMAX_DELAY);

                loggingData.start              = START_TOKEN;
                loggingData.msgCnt             = prvUptime;
                memcpy(loggingData.cellVoltage, stacksData.cellVoltage, sizeof(loggingData.cellVoltage));
                memcpy(loggingData.temperature, stacksData.temperature, sizeof(loggingData.temperature));
                loggingData.current            = adcData.current;
                loggingData.currentValid       = adcData.currentValid;
                loggingData.batteryVoltage     = adcData.batteryVoltage;
                loggingData.dcLinkVoltage      = adcData.dcLinkVoltage;
                loggingData.voltageValid       = adcData.voltageValid;
                loggingData.minCellVolt        = stacksData.minCellVolt;
                loggingData.maxCellVolt        = stacksData.maxCellVolt;
                loggingData.avgCellVolt        = stacksData.avgCellVolt;
                loggingData.cellVoltageValid   = stacksData.voltageValid;
                loggingData.minTemperature     = stacksData.minTemperature;
                loggingData.maxTemperature     = stacksData.maxTemperature;
                loggingData.avgTemperature     = stacksData.avgTemperature;
                loggingData.temperatureValid   = stacksData.temperatureValid;
                loggingData.stateMachineError  = get_contactor_error();
                loggingData.stateMachineState  = get_contactor_SM_state();
                loggingData.minSoc             = 0; //TODO: Add SOC to logs
                loggingData.maxSoc             = 0; //TODO: Add SOC to logs
                loggingData.socValid           = false; //TODO: Add SOC to logs
                loggingData.isoResistance      = 0; //TODO: Add Iso Resistance to logs
                loggingData.isoResistanceValid = false; //TODO: Add Iso Resistance to logs
                loggingData.crc16              = eeprom_get_crc16((uint8_t*)&loggingData, sizeof(logging_data_t) - sizeof(uint16_t));

                xQueueSendToBack(prvLoggingQ, &loggingData, portMAX_DELAY);

            }
        }
    }
}

void prv_logger_write_task(void *p) {
    (void) p;
    uint8_t index = 0;
    static logging_data_t element;
    static logging_data_t buf[NUMBER_OF_Q_ITEMS];

    while (1) {
        if (xQueueReceive(prvLoggingQ, &element, portMAX_DELAY)) {
            buf[index] = element;
            index++;
            if (index >= NUMBER_OF_Q_ITEMS) {
                PRINTF("Writing data to file...\n");
                UINT bw;
                volatile TickType_t start = xTaskGetTickCount();
                f_write(prvFile, (void*)&buf, NUMBER_OF_Q_ITEMS * sizeof(logging_data_t), &bw);
                f_sync(prvFile);
                volatile TickType_t end = xTaskGetTickCount();
                PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                index = 0;
            }
        }
    }
}


