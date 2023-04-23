/*!
 * @file            contactor.h
 * @brief           This module controls the Accumulator Isolation Relays (AIRs),
 *                  evaluates the system state, and controls the status LEDs
 */

/*
Copyright 2023 Luca Engelmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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
#include "stacks.h"
#include <stdbool.h>

/*!
 * @enum contactor_SM_state_t
 * Datatype for the state machine state.
 */
typedef enum {
    CONTACTOR_STATE_STANDBY,    //!< The system has no errors and can accept a TS-on request
    CONTACTOR_STATE_PRE_CHARGE, //!< The tractive system is pre-charging
    CONTACTOR_STATE_OPERATE,    //!< The tractive system is activated
    CONTACTOR_STATE_ERROR       //!< The system has critical errors and cannot accept a TS-on request
} contactor_SM_state_t;

/*!
 * @enum contactor_error_t
 * Datatype for the possible errors, which put the state machine into @ref CONTACTOR_STATE_ERROR state.
 */
typedef enum {
    ERROR_NO_ERROR                    = 0x0,    //!< No error. State machine is not in @ref CONTACTOR_STATE_ERROR state
    ERROR_IMD_FAULT                   = 0x1,    //!< IMD reports a critical isolation resistance. @ref ERROR_SDC_OPEN and @ref ERROR_IMD_POWERSTAGE_DISABLED will be set if this bit is set
    ERROR_AMS_FAULT                   = 0x2,    //!< AMS has critical values. @ref ERROR_SDC_OPEN and @ref ERROR_AMS_POWERSTAGE_DISABLED will be set if this bit is set
    ERROR_IMPLAUSIBLE_CONTACTOR       = 0x4,    //!< Contactor states are implausible
    ERROR_IMPLAUSIBLE_DC_LINK_VOLTAGE = 0x8,    //!< DC-Link voltage measurement is implausible. Fault in the wiring?
    ERROR_IMPLAUSIBLE_BATTERY_VOLTAGE = 0x10,   //!< Battery voltage measurement is implausible. Fault in the wiring?
    ERROR_IMPLAUSIBLE_CURRENT         = 0x20,   //!< Current sensor report implausible values. Fault in the wiring?
    ERROR_CURRENT_OUT_OF_RANGE        = 0x40,   //!< Current sensor reports values, which are out of the allowed range
    ERROR_PRE_CHARGE_TIMEOUT          = 0x80,   //!< Pre-charge timed out. Short circuit at the output?
    ERROR_SDC_OPEN                    = 0x100,  //!< Shutdown circuit is open
    ERROR_AMS_POWERSTAGE_DISABLED     = 0x200,  //!< AMS has an error and opened the shutdown circuit. The power stage remains disabled until the error is manually reset
    ERROR_IMD_POWERSTAGE_DISABLED     = 0x400   //!< IMD has an error and opened the shutdown circuit. The power stage remains disabled until the error is manually reset
} contactor_error_t;

/*!
 * @struct contactor_state_t
 * Datatype for the state of each relay.
 */
typedef struct {
    uint8_t negAIR_intent;      //!< Negative AIR intentional state
    uint8_t negAIR_actual;      //!< Negative AIR actual state
    uint8_t negAIR_isPlausible; //!< Negative AIR plausibility
    uint8_t posAIR_intent;      //!< Positive AIR intentional state
    uint8_t posAIR_actual;      //!< Positive AIR actual state
    uint8_t posAIR_isPlausible; //!< Positive AIR plausibility
    uint8_t pre_intent;         //!< Pre-charge relay intentional state
    uint8_t pre_actual;         //!< Pre-charge relay actual state
    uint8_t pre_isPlausible;    //!< Pre-charge relay plausibility
} contactor_state_t;

/*!
 * @brief Initialize the state machine.
 * @note This function must be called before any other function in this module can be used!
 */
void init_contactor(void);

/*!
 * @brief Request the tractive system.
 * Calling this function with active=true performs the pre-charge sequence and activates the tractive system.
 * This function must be called periodically if active=true within 500 ms. Otherwise, the state machine will switch back into standby.
 * Additionally, this function is used to clear the error state. If the state machine enters the error state while the TS is requested,
 * it is necessary to call this function with active=false to switch the state machine back into standby state.
 * @param active Request the activation or deactivation of the tractive system
 */
void request_tractive_system(bool active);

/*!
 * @brief Returns the current state of the state machine.
 * @see contactor_SM_state_t
 */
contactor_SM_state_t get_contactor_SM_state(void);

/*!
 * @brief Returns the current state of the contactors.
 * @see contactor_state_t
 */
contactor_state_t get_contactor_state(void);

/*!
 * @brief Returns the errors as bitcoded values.
 * @see contactor_error_t
 */
contactor_error_t get_contactor_error(void);


#endif /* CONTACTOR_H_ */
