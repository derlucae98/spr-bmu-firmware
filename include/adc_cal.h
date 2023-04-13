#ifndef ADC_CAL_H_
#define ADC_CAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "eeprom.h"
#include "uart.h"
#include <stdbool.h>
#include <string.h>

#define CAL_DATA_EEPROM_PAGE 0x0

typedef enum {
    CAL_STATE_STANDBY,
    CAL_STATE_WAIT_FOR_VALUE_1,
    CAL_STATE_WAIT_FOR_VALUE_2,
    CAL_STATE_UPDATE_CALIBRATION,
    CAL_STATE_FINISH
} adc_cal_state_t;

typedef struct {
    float reference;
    float current_gain;
    float current_offset;
    float ubatt_gain;
    float ubatt_offset;
    float ulink_gain;
    float ulink_offset;
} adc_calibration_t;

typedef enum {
    CAL_TYPE_UBATT_VOLT,
    CAL_TYPE_ULINK_VOLT,
    CAL_TYPE_CURRENT
} adc_cal_type_t;

void init_calibration(void);
void start_calibration(adc_cal_type_t type);
void value_applied(float value);
void cal_update_adc_value(int32_t adcValUlink, int32_t adcValUbatt, int32_t adcValCurrent);
void set_adc_reference_voltage(float voltage);
void acknowledge_calibration(void);
adc_cal_state_t get_cal_state(void);
adc_calibration_t get_adc_calibration(void);



#endif
