/*
 * status.h
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#ifndef SAFETY_H_
#define SAFETY_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "gpio.h"
#include "config.h"
#include "stacks.h"
#include "sensors.h"

typedef struct {
    bool amsStatus;
    bool amsResetStatus;
    bool imdStatus;
    bool imdResetStatus;
    bool shutdownCircuit;
    bool hvPosState;
    bool hvNegState;
    bool hvPreState;
} battery_status_t;

void init_safety(void);
void safety_task(void *p);
battery_status_t* get_battery_status(TickType_t blocktime);
void release_battery_status(void);
bool copy_battery_status(battery_status_t *dest, TickType_t blocktime);




#endif /* SAFETY_H_ */
