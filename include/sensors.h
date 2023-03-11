/*
 * sensors.h
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdbool.h>
#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mcp356x.h"
#include "spi.h"
#include "gpio.h"
#include "config.h"

typedef struct {
    float reference;
    float current_gain;
    uint16_t current_offset;
    float ubatt_gain;
    uint16_t ubatt_offset;
    float ulink_gain;
    uint16_t ulink_offset;
} adc_calibration_t;

typedef struct {
    float current;
    float batteryVoltage;
    float dcLinkVoltage;
    bool valid;
} adc_data_t;

adc_data_t* get_adc_data(TickType_t blocktime);
void release_adc_data(void);
bool copy_adc_data(adc_data_t *dest, TickType_t blocktime);

bool init_adc(void);



#endif /* SENSORS_H_ */
