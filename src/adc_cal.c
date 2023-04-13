#include "adc_cal.h"


static void prvCalTask(void *p);






adc_calibration_t get_adc_calibration(void);

static TaskHandle_t prvCalTaskHandle;

static adc_cal_state_t prvState = CAL_STATE_STANDBY;
static bool prvVoltageApplied = false;
static bool prvAckCal = false;

static void prv_update_calibration(void);
static adc_calibration_t prv_load_calibration(void);
static bool write_calibration(adc_calibration_t cal);

static float prvVal1 = 0.0f;
static int32_t prvAdcVal1 = 0;
static float prvVal2 = 0.0f;
static int32_t prvAdcVal2 = 0;
static int32_t prvAdcVal;
static float prvRef;
static float prvSlope = 0.0f;
static float prvSlopeIdeal = 0.0f;
static float prvOffset = 0.0f;
static float prvGainCal = 0.0f;

static adc_cal_type_t prvAdcCalType;

void init_calibration(void) {
    xTaskCreate(prvCalTask, "cal", CAL_TASK_STACK, NULL, CAL_TASK_PRIO, &prvCalTaskHandle);
    vTaskSuspend(prvCalTaskHandle);
}

void start_calibration(adc_cal_type_t type) {
    vTaskResume(prvCalTaskHandle);
    prvState = CAL_STATE_WAIT_FOR_VALUE_1;
    prvVoltageApplied = false;
    prvAckCal = false;
    prvAdcCalType = type;
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
    switch (prvAdcCalType) {
    case CAL_TYPE_UBATT_VOLT:
        prvAdcVal = adcValUbatt;
        break;
    case CAL_TYPE_ULINK_VOLT:
        prvAdcVal = adcValUlink;
        break;
    case CAL_TYPE_CURRENT:
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

static void prvCalTask(void *p) {
    (void) p;
    const TickType_t period = pdMS_TO_TICKS(100);
    TickType_t lastWake = xTaskGetTickCount();



    while (1) {

        switch (prvState) {
        case CAL_STATE_STANDBY:
            vTaskSuspend(prvCalTaskHandle);
            break;
        case CAL_STATE_WAIT_FOR_VALUE_1:
            if (prvVoltageApplied) {
                vTaskDelay(pdMS_TO_TICKS(20)); //Wait for new ADC value
                prvAdcVal1 = prvAdcVal;
                prvState = CAL_STATE_WAIT_FOR_VALUE_2;
                prvVoltageApplied = false;
            }
            break;
        case CAL_STATE_WAIT_FOR_VALUE_2:
            if (prvVoltageApplied) {
                vTaskDelay(pdMS_TO_TICKS(20)); //Wait for new ADC value
                prvAdcVal2 = prvAdcVal;
                prvState = CAL_STATE_UPDATE_CALIBRATION;
                prvVoltageApplied = false;
            }
            break;
        case CAL_STATE_UPDATE_CALIBRATION:
            prv_update_calibration();
            prvState = CAL_STATE_FINISH;
            break;

        case CAL_STATE_FINISH:
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
    switch (prvAdcCalType) {
    case CAL_TYPE_UBATT_VOLT:
    case CAL_TYPE_ULINK_VOLT:
        adcValue1Ideal = (-1) * prvVal1 * 8388608.0f / (ADC_VOLTAGE_CONVERSION_RATIO * prvRef);
        adcValue2Ideal = (-1) * prvVal2 * 8388608.0f / (ADC_VOLTAGE_CONVERSION_RATIO * prvRef);
        break;
    case CAL_TYPE_CURRENT:
        adcValue1Ideal = (-1) * prvVal1 * 8388608.0f / (ADC_CURRENT_CONVERSION_RATIO * prvRef);
        adcValue2Ideal = (-1) * prvVal2 * 8388608.0f / (ADC_CURRENT_CONVERSION_RATIO * prvRef);
        break;
    }
    prvSlopeIdeal = (adcValue2Ideal - adcValue1Ideal) / (prvVal2 - prvVal1);
    prvGainCal = prvSlopeIdeal / prvSlope;

    //Write to EEPROM
}

static adc_calibration_t prv_load_calibration(void) {

}

static bool write_calibration(adc_calibration_t cal) {

}



