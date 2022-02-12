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
    float ubatt_ref;
    float ubatt_gain;
    float ubatt_offset;
    float ulink_ref;
    float ulink_gain;
    float ulink_offset;
} sensor_calibration_t;

typedef struct {
    float current;
    float batteryVoltage;
    float dcLinkVoltage;
} sensor_data_t;

BaseType_t sensor_mutex_take(TickType_t blocktime);
void sensor_mutex_give(void);
extern sensor_data_t sensorData;

bool init_sensors(void);
sensor_calibration_t load_calibration(void);
void reload_calibration(void);
bool write_calibration(sensor_calibration_t cal);


#endif /* SENSORS_H_ */
