#include "logger.h"


static void prv_logger_prepare_task(void *p);
static void prv_logger_write_task(void *p);
static void prv_write_header(void);
static uint16_t prv_raw_to_csv(log_data_t *input, char *output);
static TaskHandle_t prvLoggerPrepareHandle = NULL;
static TaskHandle_t prvLoggerWriteHandle = NULL;
static QueueHandle_t prvLoggingQ = NULL;
static bool prvSdInitialized = false;
#define NUMBER_OF_Q_ITEMS 4
static FIL *prvFile = NULL;
static bool prvHeaderWritten = false;
static bool prvLoggerActive = true;


char *itoa(int value, char *str, int base); //Fix warning "implicit declaration of function 'itoa'". Works without though

void logger_init(void) {
    prvLoggingQ = xQueueCreate(NUMBER_OF_Q_ITEMS, sizeof(log_data_t));
    configASSERT(prvLoggingQ);
    xTaskCreate(prv_logger_prepare_task, "logprep", 470, NULL, 4, &prvLoggerPrepareHandle);
    xTaskCreate(prv_logger_write_task, "logwrite", 5500, NULL, 3, &prvLoggerWriteHandle);
}

void logger_set_file(bool cardStatus, FIL *file) {
    prvSdInitialized = cardStatus;
    prvFile = file;
}

void logger_control(bool active) {
    prvLoggerActive = active;
}

void logger_tick_hook(uint32_t uptime) {
    (void) uptime;
}

void prv_logger_prepare_task(void *p) {
    (void) p;
    log_data_t loggingData;
    memset(&loggingData, 0, sizeof(log_data_t));
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(250);

    while (1) {
        if (prvLoggerPrepareHandle) {
            if (prvFile == NULL) {
                vTaskDelayUntil(&xLastWakeTime, xPeriod);
                continue;
            }

            if (prvSdInitialized && !prvHeaderWritten) {
                prv_write_header();
                prvHeaderWritten = true;
            }

            if (prvSdInitialized && prvLoggerActive) {
                copy_stacks_data(&loggingData.stacksData, pdMS_TO_TICKS(20));
                copy_adc_data(&loggingData.adcData, pdMS_TO_TICKS(20));
                loggingData.soc = get_soc_stats();
                loggingData.stateMachineError = get_contactor_error();
                loggingData.tsState = get_contactor_SM_state();
                loggingData.timestamp = rtc_get_unix_time();
                loggingData.resetReason = get_reset_reason();
                loggingData.relativeTime++;

                xQueueSendToBack(prvLoggingQ, &loggingData, portMAX_DELAY);
            }

        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void prv_logger_write_task(void *p) {
    (void) p;
    size_t index = 0;
    uint8_t items = 0;
    static log_data_t element;
    static log_data_t buf[NUMBER_OF_Q_ITEMS];

    char buffer[20000];
    memset(buffer, 0, sizeof(buffer));

    while (1) {
        if (xQueueReceive(prvLoggingQ, &element, portMAX_DELAY)) {
            buf[items] = element;
            items++;
            if (items >= NUMBER_OF_Q_ITEMS) {
                volatile TickType_t start = xTaskGetTickCount();
                for (size_t i = 0; i < NUMBER_OF_Q_ITEMS; i++) {
                    index += prv_raw_to_csv(&buf[i], &buffer[index]);
                }
                index = 0;
                items = 0;

                UINT bw;
                if (get_peripheral_mutex(pdMS_TO_TICKS(1000))) {
                    DSTATUS stat = f_write(prvFile, (void*)&buffer, 7700, &bw);
                    f_sync(prvFile);
                    volatile TickType_t end = xTaskGetTickCount();
                    PRINTF("Logger: %lu bytes written! Took %lu ms\n", bw, end - start);
                    if (stat != 0) {
                        PRINTF("Logger: failed to log\n");
                    }
                    release_peripheral_mutex();
                } else {
                    PRINTF("Peripheral mutex lock failed!\n");
                }
            }
        }
    }
}

static void prv_write_header(void) {
    static const char *header =
        // Thanks ChatGPT:
        "timestamp;relative time;Battery Voltage;TSAC output voltage;voltage valid;"
        "current;current valid;"
        "Cell 1;Cell 2;Cell 3;Cell 4;Cell 5;Cell 6;Cell 7;Cell 8;Cell 9;Cell 10;Cell 11;Cell 12;"
        "Cell 13;Cell 14;Cell 15;Cell 16;Cell 17;Cell 18;Cell 19;Cell 20;Cell 21;Cell 22;Cell 23;Cell 24;"
        "Cell 25;Cell 26;Cell 27;Cell 28;Cell 29;Cell 30;Cell 31;Cell 32;Cell 33;Cell 34;Cell 35;Cell 36;"
        "Cell 37;Cell 38;Cell 39;Cell 40;Cell 41;Cell 42;Cell 43;Cell 44;Cell 45;Cell 46;Cell 47;Cell 48;"
        "Cell 49;Cell 50;Cell 51;Cell 52;Cell 53;Cell 54;Cell 55;Cell 56;Cell 57;Cell 58;Cell 59;Cell 60;"
        "Cell 61;Cell 62;Cell 63;Cell 64;Cell 65;Cell 66;Cell 67;Cell 68;Cell 69;Cell 70;Cell 71;Cell 72;"
        "Cell 73;Cell 74;Cell 75;Cell 76;Cell 77;Cell 78;Cell 79;Cell 80;Cell 81;Cell 82;Cell 83;Cell 84;"
        "Cell 85;Cell 86;Cell 87;Cell 88;Cell 89;Cell 90;Cell 91;Cell 92;Cell 93;Cell 94;Cell 95;Cell 96;"
        "Cell 97;Cell 98;Cell 99;Cell 100;Cell 101;Cell 102;Cell 103;Cell 104;Cell 105;Cell 106;Cell 107;Cell 108;"
        "Cell 109;Cell 110;Cell 111;Cell 112;Cell 113;Cell 114;Cell 115;Cell 116;Cell 117;Cell 118;Cell 119;Cell 120;"
        "Cell 121;Cell 122;Cell 123;Cell 124;Cell 125;Cell 126;Cell 127;Cell 128;Cell 129;Cell 130;Cell 131;Cell 132;"
        "Cell 133;Cell 134;Cell 135;Cell 136;Cell 137;Cell 138;Cell 139;Cell 140;Cell 141;Cell 142;Cell 143;Cell 144;"
        "Temp 1;Temp 2;Temp 3;Temp 4;Temp 5;Temp 6;Temp 7;Temp 8;Temp 9;Temp 10;Temp 11;Temp 12;"
        "Temp 13;Temp 14;Temp 15;Temp 16;Temp 17;Temp 18;Temp 19;Temp 20;Temp 21;Temp 22;Temp 23;Temp 24;"
        "Temp 25;Temp 26;Temp 27;Temp 28;Temp 29;Temp 30;Temp 31;Temp 32;Temp 33;Temp 34;Temp 35;Temp 36;"
        "Temp 37;Temp 38;Temp 39;Temp 40;Temp 41;Temp 42;Temp 43;Temp 44;Temp 45;Temp 46;Temp 47;Temp 48;"
        "Temp 49;Temp 50;Temp 51;Temp 52;Temp 53;Temp 54;Temp 55;Temp 56;Temp 57;Temp 58;Temp 59;Temp 60;"
        "Temp 61;Temp 62;Temp 63;Temp 64;Temp 65;Temp 66;Temp 67;Temp 68;Temp 69;Temp 70;Temp 71;Temp 72;"
        "Min Cell Voltage;Max Cell Voltage;Avg Cell Voltage;Cell Voltage valid;Min Temperature;Max Temperature;Avg Temperature;"
        "Temperature valid;TS state;error code;reset reason;Min SOC;Max SOC;SOC valid;isolation resistance;isolation resistance valid;"
        "Cell status 1;Cell status 2;Cell status 3;Cell status 4;Cell status 5;Cell status 6;Cell status 7;Cell status 8;Cell status 9;Cell status 10;Cell status 11;Cell status 12;"
        "Cell status 13;Cell status 14;Cell status 15;Cell status 16;Cell status 17;Cell status 18;Cell status 19;Cell status 20;Cell status 21;Cell status 22;Cell status 23;Cell status 24;"
        "Cell status 25;Cell status 26;Cell status 27;Cell status 28;Cell status 29;Cell status 30;Cell status 31;Cell status 32;Cell status 33;Cell status 34;Cell status 35;Cell status 36;"
        "Cell status 37;Cell status 38;Cell status 39;Cell status 40;Cell status 41;Cell status 42;Cell status 43;Cell status 44;Cell status 45;Cell status 46;Cell status 47;Cell status 48;"
        "Cell status 49;Cell status 50;Cell status 51;Cell status 52;Cell status 53;Cell status 54;Cell status 55;Cell status 56;Cell status 57;Cell status 58;Cell status 59;Cell status 60;"
        "Cell status 61;Cell status 62;Cell status 63;Cell status 64;Cell status 65;Cell status 66;Cell status 67;Cell status 68;Cell status 69;Cell status 70;Cell status 71;Cell status 72;"
        "Cell status 73;Cell status 74;Cell status 75;Cell status 76;Cell status 77;Cell status 78;Cell status 79;Cell status 80;Cell status 81;Cell status 82;Cell status 83;Cell status 84;"
        "Cell status 85;Cell status 86;Cell status 87;Cell status 88;Cell status 89;Cell status 90;Cell status 91;Cell status 92;Cell status 93;Cell status 94;Cell status 95;Cell status 96;"
        "Cell status 97;Cell status 98;Cell status 99;Cell status 100;Cell status 101;Cell status 102;Cell status 103;Cell status 104;Cell status 105;Cell status 106;Cell status 107;Cell status 108;"
        "Cell status 109;Cell status 110;Cell status 111;Cell status 112;Cell status 113;Cell status 114;Cell status 115;Cell status 116;Cell status 117;Cell status 118;Cell status 119;Cell status 120;"
        "Cell status 121;Cell status 122;Cell status 123;Cell status 124;Cell status 125;Cell status 126;Cell status 127;Cell status 128;Cell status 129;Cell status 130;Cell status 131;Cell status 132;"
        "Cell status 133;Cell status 134;Cell status 135;Cell status 136;Cell status 137;Cell status 138;Cell status 139;Cell status 140;Cell status 141;Cell status 142;Cell status 143;Cell status 144;"
        "Temp status 1;Temp status 2;Temp status 3;Temp status 4;Temp status 5;Temp status 6;Temp status 7;Temp status 8;Temp status 9;Temp status 10;Temp status 11;Temp status 12;"
        "Temp status 13;Temp status 14;Temp status 15;Temp status 16;Temp status 17;Temp status 18;Temp status 19;Temp status 20;Temp status 21;Temp status 22;Temp status 23;Temp status 24;"
        "Temp status 25;Temp status 26;Temp status 27;Temp status 28;Temp status 29;Temp status 30;Temp status 31;Temp status 32;Temp status 33;Temp status 34;Temp status 35;Temp status 36;"
        "Temp status 37;Temp status 38;Temp status 39;Temp status 40;Temp status 41;Temp status 42;Temp status 43;Temp status 44;Temp status 45;Temp status 46;Temp status 47;Temp status 48;"
        "Temp status 49;Temp status 50;Temp status 51;Temp status 52;Temp status 53;Temp status 54;Temp status 55;Temp status 56;Temp status 57;Temp status 58;Temp status 59;Temp status 60;"
        "Temp status 61;Temp status 62;Temp status 63;Temp status 64;Temp status 65;Temp status 66;Temp status 67;Temp status 68;Temp status 69;Temp status 70;Temp status 71;Temp status 72\r\n";



    if (get_peripheral_mutex(pdMS_TO_TICKS(1000))) {
        UINT bw;
        PRINTF("Writing header...\n");
        DSTATUS stat = f_write(prvFile, header, strlen(header), &bw);
        f_sync(prvFile);
        PRINTF("Done! %u, %u bytes written!\n", stat, bw);
        release_peripheral_mutex();
    } else {
        PRINTF("Peripheral mutex lock failed!\n");
    }
}

static uint16_t prv_raw_to_csv(log_data_t *input, char *output) {
    uint16_t offset = 0;

    // Unix time
    itoa(input->timestamp, output, 10);
    output[10] = ';';
    offset += 11;

    //Relative time; Use snprintf to simplify adding leading zeros
    snprintf(output + offset, 12, "%010lu;", input->relativeTime);
    offset += 11;

    // Battery voltage
    if (input->adcData.batteryVoltage < 0.0f) {
        input->adcData.batteryVoltage = 0.0f; // Limit value to positive number in case of slight inaccuracies to suppress sign in snprintf output
    }
    snprintf(output + offset, 7, "%05.1f;", input->adcData.batteryVoltage);
    offset += 6;

    // TSAC output voltage and voltage validity
    if (input->adcData.dcLinkVoltage < 0.0f) {
        input->adcData.dcLinkVoltage = 0.0f; // Limit value to positive number in case of slight inaccuracies to suppress sign in snprintf output
    }
    snprintf(output + offset, 9, "%05.1f;%u;", input->adcData.dcLinkVoltage, input->adcData.voltageValid);
    offset += 8;

    // Current and validity
    snprintf(output + offset, 11, "%07.2f;%u;", input->adcData.current, input->adcData.currentValid);
    offset += 10;

    //Cell voltages
    for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
        // Convert fixed-point data to ascii string, move decimal places to the right and manually add decimal point and semicolon
        // Insanely fast compared to snprintf
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (input->stacksData.cellVoltage[stack][cell] != 0) {
                itoa(input->stacksData.cellVoltage[stack][cell], output + offset, 10);
                memmove(output + 2 + offset, output + 1 + offset, 4);
                output[offset + 1] = '.';
                output[offset + 6] = ';';
                offset += 7;
            } else {
                //Catch edge case daisy chain error: Cell voltages are 0
                output[offset + 0] = '0';
                output[offset + 1] = '.';
                output[offset + 2] = '0';
                output[offset + 3] = '0';
                output[offset + 4] = '0';
                output[offset + 5] = '0';
                output[offset + 6] = ';';
                offset += 7;
            }
        }
    }

    //Cell temperatures
    for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
        // Convert fixed-point data to ascii string, move decimal places to the right and manually add decimal point and semicolon
        // Insanely fast compared to snprintf
        for (size_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            itoa(input->stacksData.temperature[stack][tempsens], output + offset, 10);

            if (input->stacksData.temperature[stack][tempsens] < 100 && input->stacksData.temperature[stack][tempsens] > 0) {
                // Add leading zero if value < 10.0 °C
                memmove(output + 1 + offset, output + offset, 2);
                output[offset] = '0';
            }
            if (input->stacksData.temperature[stack][tempsens] == 0) {
                // Edge case: 0.00 °C, open sensor or daisy chain error: We have to add two zeros
                memmove(output + 2 + offset, output + offset, 1);
                output[offset] = '0';
                output[offset + 1] = '0';
            }
            memmove(output + 3 + offset, output + 2 + offset, 1);
            output[offset + 2] = '.';
            output[offset + 4] = ';';
            offset += 5;
        }
    }

    // Minimum cell voltage
    if (input->stacksData.minCellVolt != 0) {
        itoa(input->stacksData.minCellVolt, output + offset, 10);
        memmove(output + 2 + offset, output + 1 + offset, 4);
        output[offset + 1] = '.';
        output[offset + 6] = ';';
        offset += 7;
    } else {
        //Catch edge case daisy chain error: Cell voltages are 0
        output[offset + 0] = '0';
        output[offset + 1] = '.';
        output[offset + 2] = '0';
        output[offset + 3] = '0';
        output[offset + 4] = '0';
        output[offset + 5] = '0';
        output[offset + 6] = ';';
        offset += 7;
    }

    // Maximum cell voltage
    if (input->stacksData.maxCellVolt != 0) {
        itoa(input->stacksData.maxCellVolt, output + offset, 10);
        memmove(output + 2 + offset, output + 1 + offset, 4);
        output[offset + 1] = '.';
        output[offset + 6] = ';';
        offset += 7;
    } else {
        //Catch edge case daisy chain error: Cell voltages are 0
        output[offset + 0] = '0';
        output[offset + 1] = '.';
        output[offset + 2] = '0';
        output[offset + 3] = '0';
        output[offset + 4] = '0';
        output[offset + 5] = '0';
        output[offset + 6] = ';';
        offset += 7;
    }

    // Average cell voltage
    if (input->stacksData.avgCellVolt != 0) {
        itoa(input->stacksData.avgCellVolt, output + offset, 10);
        memmove(output + 2 + offset, output + 1 + offset, 4);
        output[offset + 1] = '.';
        output[offset + 6] = ';';
        offset += 7;
    } else {
        //Catch edge case daisy chain error: Cell voltages are 0
        output[offset + 0] = '0';
        output[offset + 1] = '.';
        output[offset + 2] = '0';
        output[offset + 3] = '0';
        output[offset + 4] = '0';
        output[offset + 5] = '0';
        output[offset + 6] = ';';
        offset += 7;
    }

    // Voltage validity
    itoa(input->stacksData.voltageValid, output + offset, 10);
    output[offset + 1] = ';';
    offset += 2;

    // Minimum cell temperature
    itoa(input->stacksData.minTemperature, output + offset, 10);

    if (input->stacksData.minTemperature < 100 && input->stacksData.minTemperature > 0) {
        // Add leading zero if value < 10.0 °C
        memmove(output + 1 + offset, output + offset, 2);
        output[offset] = '0';
    }
    if (input->stacksData.minTemperature == 0) {
        // Edge case: 0.00 °C, open sensor or daisy chain error: We have to add two zeros
        memmove(output + 2 + offset, output + offset, 1);
        output[offset] = '0';
        output[offset + 1] = '0';
    }
    memmove(output + 3 + offset, output + 2 + offset, 1);
    output[offset + 2] = '.';
    output[offset + 4] = ';';
    offset += 5;

    // Maximum cell temperature
    itoa(input->stacksData.maxTemperature, output + offset, 10);

    if (input->stacksData.maxTemperature < 100 && input->stacksData.maxTemperature > 0) {
        // Add leading zero if value < 10.0 °C
        memmove(output + 1 + offset, output + offset, 2);
        output[offset] = '0';
    }
    if (input->stacksData.maxTemperature == 0) {
        // Edge case: 0.00 °C, open sensor or daisy chain error: We have to add two zeros
        memmove(output + 2 + offset, output + offset, 1);
        output[offset] = '0';
        output[offset + 1] = '0';
    }
    memmove(output + 3 + offset, output + 2 + offset, 1);
    output[offset + 2] = '.';
    output[offset + 4] = ';';
    offset += 5;

    // Avg cell temperature
    itoa(input->stacksData.avgTemperature, output + offset, 10);

    if (input->stacksData.avgTemperature < 100 && input->stacksData.avgTemperature > 0) {
        // Add leading zero if value < 10.0 °C
        memmove(output + 1 + offset, output + offset, 2);
        output[offset] = '0';
    }
    if (input->stacksData.avgTemperature == 0) {
        // Edge case: 0.00 °C, open sensor or daisy chain error: We have to add two zeros
        memmove(output + 2 + offset, output + offset, 1);
        output[offset] = '0';
        output[offset + 1] = '0';
    }
    memmove(output + 3 + offset, output + 2 + offset, 1);
    output[offset + 2] = '.';
    output[offset + 4] = ';';
    offset += 5;

    // Temperature validity
    itoa(input->stacksData.temperatureValid, output + offset, 10);
    output[offset + 1] = ';';
    offset += 2;

    // TS state
    itoa(input->tsState, output + offset, 10);
    output[offset + 1] = ';';
    offset += 2;

    // Error code
    snprintf(output + offset, 10, "%08X;", input->stateMachineError);
    offset += 9;

    // Reset Reason
    snprintf(output + offset, 10, "%08X;", (unsigned int)input->resetReason);
    offset += 9;

    // Min SOC
    uint8_t minSoc = (uint8_t) input->soc.minSoc;
    itoa(minSoc, output + offset, 10);
    output[offset + 3] = ';';
    offset += 4;

    // Max SOC
    uint8_t maxSoc = (uint8_t) input->soc.maxSoc;
    itoa(maxSoc, output + offset, 10);
    output[offset + 3] = ';';
    offset += 4;

    // SOC valid
    itoa(input->soc.valid, output + offset, 10);
    output[offset + 1] = ';';
    offset += 2;

    // Iso resistance
    itoa(input->isoResistance, output + offset, 10);
    output[offset + 4] = ';';
    offset += 5;

    // Iso resistance valid
    itoa(input->isoResistanceValid, output + offset, 10);
    output[offset + 1] = ';';
    offset += 2;

    //Cell voltage status
    for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            itoa(input->stacksData.cellVoltageStatus[stack][cell], output + offset, 10);
            output[offset + 1] = ';';
            offset += 2;
        }
    }

    //Temperature status
    for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
        for (size_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            itoa(input->stacksData.temperatureStatus[stack][tempsens], output + offset, 10);
            output[offset + 1] = ';';
            offset += 2;
        }
    }
    output[offset - 1] = '\r'; // override last ';'
    output[offset] = '\n';
    offset += 2;
    return offset;
}



