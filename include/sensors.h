/*
 * sensors.h
 *
 *  Created on: Feb 4, 2022
 *      Author: scuderia
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

bool init_sensors();


#endif /* SENSORS_H_ */
