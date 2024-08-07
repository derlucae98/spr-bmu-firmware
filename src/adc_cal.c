#include "adc_cal.h"


static void prvCalTask(void *p);

static TaskHandle_t prvCalTaskHandle;

static adc_cal_state_t prvState = CAL_STATE_STANDBY;
static bool prvVoltageApplied = false;
static bool prvAckCal = false;

static void prv_update_calibration(void);
static void prv_load_calibration(void);
static void prv_write_calibration(void);
static void prv_default_calibration(void);

static float prvVal1 = 0.0f;
static int32_t prvAdcVal1 = 0;
static float prvVal2 = 0.0f;
static int32_t prvAdcVal2 = 0;
static int32_t prvAdcVal;
static float prvRef = 0.0f;
static float prvSlope = 0.0f;
static float prvSlopeIdeal = 0.0f;
static float prvOffset = 0.0f;
static float prvGainCal = 0.0f;

static adc_calibration_t prvAdcCal;

static adc_cal_input_t prvAdcCalInput;

void init_calibration(void) {
    xTaskCreate(prvCalTask, "cal", CAL_TASK_STACK, NULL, CAL_TASK_PRIO, &prvCalTaskHandle);
    vTaskSuspend(prvCalTaskHandle);
    prv_load_calibration();
}

void start_calibration(adc_cal_input_t input) {
    prvState = CAL_STATE_WAIT_FOR_VALUE_1;
    vTaskResume(prvCalTaskHandle);
    prvVoltageApplied = false;
    prvAckCal = false;
    prvAdcCalInput = input;
}

void value_applied(float value) {
    if (prvState == CAL_STATE_WAIT_FOR_VALUE_1) {
        prvVal1 = value;
    } else if (prvState == CAL_STATE_WAIT_FOR_VALUE_2) {
        prvVal2 = value;
    }
    prvVoltageApplied = true;
}

void cal_update_adc_value(int32_t adcValUlink, int32_t adcValUbatt, int32_t adcValCurrent) {
    switch (prvAdcCalInput) {
    case CAL_INPUT_UBATT_VOLT:
        prvAdcVal = adcValUbatt;
        break;
    case CAL_INPUT_ULINK_VOLT:
        prvAdcVal = adcValUlink;
        break;
    case CAL_INPUT_CURRENT:
        prvAdcVal = adcValCurrent;
        break;
    }
}

adc_cal_state_t get_cal_state(void) {
    return prvState;
}

void set_adc_reference_voltage(float voltage) {
    prvRef = voltage;
}

void acknowledge_calibration(void) {
    prvAckCal = true;
}

adc_calibration_t get_adc_calibration(void) {
    return prvAdcCal;
}

static void prvCalTask(void *p) {
    (void) p;
    const TickType_t period = pdMS_TO_TICKS(100);
    TickType_t lastWake = xTaskGetTickCount();

    while (1) {

        switch (prvState) {
        case CAL_STATE_STANDBY:
            PRINTF("CAL Standby\n");
            vTaskSuspend(prvCalTaskHandle);
            prvAckCal = false;
            break;
        case CAL_STATE_WAIT_FOR_VALUE_1:
            PRINTF("CAL Wait for val 1\n");
            prvAckCal = false;
            if (prvVoltageApplied) {
                vTaskDelay(pdMS_TO_TICKS(20)); //Wait for new ADC value
                prvAdcVal1 = prvAdcVal;
                prvState = CAL_STATE_WAIT_FOR_VALUE_2;
                prvVoltageApplied = false;
            }
            break;
        case CAL_STATE_WAIT_FOR_VALUE_2:
            PRINTF("CAL Wait for val 2\n");
            prvAckCal = false;
            if (prvVoltageApplied) {
                vTaskDelay(pdMS_TO_TICKS(20)); //Wait for new ADC value
                prvAdcVal2 = prvAdcVal;
                prvState = CAL_STATE_UPDATE_CALIBRATION;
                prvVoltageApplied = false;
            }
            break;
        case CAL_STATE_UPDATE_CALIBRATION:
            PRINTF("CAL Update calibration\n");
            prvAckCal = false;
            prv_update_calibration();
            prvState = CAL_STATE_FINISH;
            break;
        case CAL_STATE_FINISH:
            PRINTF("CAL finish\n");
            if (prvAckCal) {
                prvState = CAL_STATE_STANDBY;
            }
            break;
        }

        vTaskDelayUntil(&lastWake, period);
    }
}

static void prv_update_calibration(void) {
    prvSlope = (prvAdcVal2 - prvAdcVal1) / (prvVal2 - prvVal1);
    prvOffset = prvSlope * (0 - prvVal1) + prvAdcVal1;

    float adcValue1Ideal = 0.0f;
    float adcValue2Ideal = 0.0f;

    if (prvRef == 0.0f) { //Comparing against 0.0f is safe here. If the value will not be updated with set_adc_reference_voltage(), then prvRef will have the value 0.0f
        prvRef = prvAdcCal.reference;
    } else {
        prvAdcCal.reference = prvRef;
    }

    switch (prvAdcCalInput) {
    case CAL_INPUT_UBATT_VOLT:
    case CAL_INPUT_ULINK_VOLT:
        adcValue1Ideal = (-1) * prvVal1 * 8388608.0f / (ADC_VOLTAGE_CONVERSION_RATIO * prvRef);
        adcValue2Ideal = (-1) * prvVal2 * 8388608.0f / (ADC_VOLTAGE_CONVERSION_RATIO * prvRef);
        break;
    case CAL_INPUT_CURRENT:
        adcValue1Ideal = prvVal1 * 8388608.0f / (ADC_CURRENT_CONVERSION_RATIO * prvRef);
        adcValue2Ideal = prvVal2 * 8388608.0f / (ADC_CURRENT_CONVERSION_RATIO * prvRef);
        break;
    }

    prvSlopeIdeal = (adcValue2Ideal - adcValue1Ideal) / (prvVal2 - prvVal1);
    prvGainCal = prvSlopeIdeal / prvSlope;

    switch (prvAdcCalInput) {
        case CAL_INPUT_UBATT_VOLT:
            prvAdcCal.ubatt_gain = prvGainCal;
            prvAdcCal.ubatt_offset = prvOffset;
            break;
        case CAL_INPUT_ULINK_VOLT:
            prvAdcCal.ulink_gain = prvGainCal;
            prvAdcCal.ulink_offset = prvOffset;
            break;
        case CAL_INPUT_CURRENT:
            prvAdcCal.current_gain = prvGainCal;
            prvAdcCal.current_offset = prvOffset;
            break;
    }

    prv_write_calibration();
    prv_load_calibration();
}

static void prv_load_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0, EEPROM_PAGESIZE);
    bool uncal = true;

    bool ret;
    ret = eeprom_read(pageBuffer, CAL_DATA_EEPROM_PAGE, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    if (ret == false) {
        PRINTF("Unable to read from EEPROM!\n");
        configASSERT(0);
    }
    uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];


    for (size_t i = 0; i < EEPROM_PAGESIZE; i++) {
        if (pageBuffer[i] != 0xFF) {
            uncal = false;
        }
    }

    if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading calibration data!\n");
        prv_default_calibration();
    }

    if (uncal) {
        PRINTF("No calibration data found!\n");
        prv_default_calibration();
        return;
    }

    memcpy(&prvAdcCal, &pageBuffer[0], sizeof(adc_calibration_t));
}

static void prv_write_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0xFF, sizeof(pageBuffer));
    memcpy(&pageBuffer[0], &prvAdcCal, sizeof(adc_calibration_t));
    uint16_t crc = eeprom_get_crc16(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t));
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;

    bool ret;
    ret = eeprom_write(pageBuffer, CAL_DATA_EEPROM_PAGE, sizeof(pageBuffer), pdMS_TO_TICKS(500));

    if (ret == false) {
        PRINTF("Unable to write to EEPROM!\n");
        configASSERT(0);
    }

    uint16_t timeout = 500; //5 seconds
    while (eeprom_busy(pdMS_TO_TICKS(500))) {
        if (--timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            PRINTF("Writing calibration data to EEPROM timed out!\n");
            configASSERT(0);
        }
    }
}

static void prv_default_calibration(void) {
    PRINTF("ADC: Initializing with default calibration.\n");
    prvAdcCal.reference = 2.5f;
    prvAdcCal.current_gain = 1.0f;
    prvAdcCal.current_offset = 0.0f;
    prvAdcCal.ubatt_gain = 1.0f;
    prvAdcCal.ubatt_offset = 0.0f;
    prvAdcCal.ulink_gain = 1.0f;
    prvAdcCal.ulink_offset = 0.0f;
    prv_write_calibration();
}


