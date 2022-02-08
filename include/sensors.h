/*
 * sensors.h
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdbool.h>
#include "S32K146.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mcp356x.h"
#include "spi.h"
#include "gpio.h"
#include "gpio_def.h"
#include "LTC6811.h"
#include "eeprom.h"

#define SENSOR_SPI LPSPI1

typedef struct {
    float current_1_ref;    //Main sensor board
    float current_1_gain;   //Main sensor board
    float current_1_offset; //Main sensor board
    float current_2_ref;    //Spare sensor board
    float current_2_gain;   //Spare sensor board
    float current_2_offset; //Spare sensor board
    float voltage_1_ref;
    float voltage_1_gain;
    float voltage_1_offset;
    float voltage_2_ref;
    float voltage_2_gain;
    float voltage_2_offset;
    float voltage_3_ref;
    float voltage_3_gain;
    float voltage_3_offset;
    float voltage_4_ref;
    float voltage_4_gain;
    float voltage_4_offset;
    float voltage_5_ref;
    float voltage_5_gain;
    float voltage_5_offset;
} sensor_calibration_t;

bool init_sensors(void);
sensor_calibration_t load_calibration(void);
bool write_calibration(sensor_calibration_t cal);


#endif /* SENSORS_H_ */
