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


bool init_sensors(void);
void load_calibration(void);


#endif /* SENSORS_H_ */
