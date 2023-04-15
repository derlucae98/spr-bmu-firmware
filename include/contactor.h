/*
 * contactor.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef CONTACTOR_H_
#define CONTACTOR_H_

#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "gpio.h"
#include "config.h"
#include "wdt.h"
#include "math.h"
#include "uart.h"
#include "adc.h"
#include <stdbool.h>

typedef enum {
    CONTACTOR_STATE_STANDBY,
    CONTACTOR_STATE_PRE_CHARGE,
    CONTACTOR_STATE_OPERATE,
    CONTACTOR_STATE_ERROR
} contactor_SM_state_t;

typedef enum {
    ERROR_NO_ERROR                    = 0x0,
    ERROR_IMD_FAULT                   = 0x1,
    ERROR_AMS_FAULT                   = 0x2,
    ERROR_IMPLAUSIBLE_CONTACTOR       = 0x4,
    ERROR_IMPLAUSIBLE_DC_LINK_VOLTAGE = 0x8,
    ERROR_IMPLAUSIBLE_BATTERY_VOLTAGE = 0x10,
    ERROR_PRE_CHARGE_TIMEOUT          = 0x20,
    ERROR_SDC_OPEN                    = 0x40,
    ERROR_AMS_POWERSTAGE_DISABLED     = 0x80,
    ERROR_IMD_POWERSTAGE_DISABLED     = 0x100
} contactor_error_t;

typedef struct {
    uint8_t negAIR_intent;
    uint8_t negAIR_actual;
    uint8_t negAIR_isPlausible;
    uint8_t posAIR_intent;
    uint8_t posAIR_actual;
    uint8_t posAIR_isPlausible;
    uint8_t pre_intent;
    uint8_t pre_actual;
    uint8_t pre_isPlausible;
} contactor_state_t;

contactor_SM_state_t get_contactor_SM_state(void);
contactor_state_t get_contactor_state(void);
contactor_error_t get_contactor_error(void);

void init_contactor(void);
void request_tractive_system(bool active);


#endif /* CONTACTOR_H_ */
