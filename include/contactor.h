/*
 * contactor.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef CONTACTOR_H_
#define CONTACTOR_H_

#include "S32K146.h"
#include "FreeRTOS.h"
#include "task.h"
#include "gpio.h"
#include "config.h"

typedef enum {
    EVENT_NONE,
    EVENT_TS_ACTIVATE,
    EVENT_TS_DEACTIVATE,
    EVENT_PRE_CHARGE_SUCCESSFUL,
    EVENT_ERROR,
    EVENT_ERROR_CLEARED
} event_t;

extern TaskHandle_t contactor_control_task_handle;
extern volatile event_t contactorEvent;
void init_contactor(void);


#endif /* CONTACTOR_H_ */
