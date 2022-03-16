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
#include <stdbool.h>
#include "sensors.h"
#include "safety.h"


typedef enum {
    EVENT_NONE,
    EVENT_TS_ACTIVATE,
    EVENT_TS_DEACTIVATE,
    EVENT_PRE_CHARGE_SUCCESSFUL,
    EVENT_ERROR,
    EVENT_ERROR_CLEARED
} event_t;

typedef enum {
    STATE_STANDBY,
    STATE_PRE_CHARGE,
    STATE_OPERATE,
    STATE_ERROR
} state_t;

typedef enum {
    ERROR_NO_ERROR,
    ERROR_SYSTEM_NOT_HEALTHY,
    ERROR_CONTACTOR_IMPLAUSIBLE,
    ERROR_PRE_CHARGE_TOO_SHORT,
    ERROR_PRE_CHARGE_TIMEOUT
} error_t;

state_t get_contactor_state(void);
error_t get_contactor_error(void);
void init_contactor(void);
void request_tractive_system(bool active);


#endif /* CONTACTOR_H_ */
