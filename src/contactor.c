/*!
 * @file            contactor.c
 * @brief           This module controls the Accumulator Isolation Relays (AIRs)
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

#include "contactor.h"

static void prv_contactor_control_task(void *p);
static void prv_system_state_task(void *p);

static void prv_standby(void);
static void prv_pre_charge(void);
static void prv_operate(void);
static void prv_error(void);

static void prv_open_shutdown_circuit(void);
static void prv_close_shutdown_circuit(void);

static bool prvTsActive = false;
static uint8_t prvTsRequestTimeout = 0;
static uint8_t prvPrechargeTimeout = 0;
static contactor_SM_state_t prvStateMachineState = CONTACTOR_STATE_STANDBY;
static uint32_t prvStateMachineError = ERROR_NO_ERROR;
static contactor_state_t prvContactorState;

static bool prvAmsFault = false;
static bool prvImdFault = false;
static bool prvVoltageFault = false;
static bool prvCurrentFault = false;
static bool prvCurrentOutOfRange = false;
static bool prvAmsPowerStageDisabled = false;
static bool prvImdPowerStageDisabled = false;
static bool prvSdcOpen = false;

typedef enum {
    EVENT_NONE,
    EVENT_TS_ACTIVATE,
    EVENT_TS_DEACTIVATE,
    EVENT_PRE_CHARGE_SUCCESSFUL,
    EVENT_ERROR,
    EVENT_ERROR_CLEARED
} event_t;

typedef struct {
    contactor_SM_state_t current;
} state_machine_t;

typedef struct {
    contactor_SM_state_t current;
    event_t event;
    contactor_SM_state_t next;
} state_array_t;

static state_array_t prvStateArray[] = {
    {CONTACTOR_STATE_STANDBY,    EVENT_TS_ACTIVATE,           CONTACTOR_STATE_PRE_CHARGE},
    {CONTACTOR_STATE_STANDBY,    EVENT_ERROR,                 CONTACTOR_STATE_ERROR},
    {CONTACTOR_STATE_PRE_CHARGE, EVENT_TS_DEACTIVATE,         CONTACTOR_STATE_STANDBY},
    {CONTACTOR_STATE_PRE_CHARGE, EVENT_ERROR,                 CONTACTOR_STATE_ERROR},
    {CONTACTOR_STATE_PRE_CHARGE, EVENT_PRE_CHARGE_SUCCESSFUL, CONTACTOR_STATE_OPERATE},
    {CONTACTOR_STATE_OPERATE,    EVENT_ERROR,                 CONTACTOR_STATE_ERROR},
    {CONTACTOR_STATE_OPERATE,    EVENT_TS_DEACTIVATE,         CONTACTOR_STATE_STANDBY},
    {CONTACTOR_STATE_ERROR,      EVENT_ERROR_CLEARED,         CONTACTOR_STATE_STANDBY}
};

typedef struct {
    const char* name;
    void (*function)(void);
} state_function_t;

static state_function_t prvStateFunction[] = {
    {"STATE_STANDBY",    &prv_standby},
    {"STATE_PRE_CHARGE", &prv_pre_charge},
    {"STATE_OPERATE",    &prv_operate},
    {"STATE_ERROR",      &prv_error}
};

static state_machine_t prvStateMachine;
static event_t prvEvent = EVENT_NONE;

typedef enum {
    CONTACTOR_HV_POS,
    CONTACTOR_HV_NEG,
    CONTACTOR_HV_PRE
} contactor_t;

static inline void prv_open_contactor(contactor_t contactor) {
    switch (contactor) {
        case CONTACTOR_HV_POS:
            clear_pin(AIR_POS_SET_PORT, AIR_POS_SET_PIN);
            clear_pin(AIR_POS_CLR_PORT, AIR_POS_CLR_PIN);
            break;
        case CONTACTOR_HV_NEG:
            clear_pin(AIR_NEG_SET_PORT, AIR_NEG_SET_PIN);
            clear_pin(AIR_NEG_CLR_PORT, AIR_NEG_CLR_PIN);
            break;
        case CONTACTOR_HV_PRE:
            clear_pin(AIR_PRE_SET_PORT, AIR_PRE_SET_PIN);
            clear_pin(AIR_PRE_CLR_PORT, AIR_PRE_CLR_PIN);
            break;
    }
}

static inline void prv_close_contactor(contactor_t contactor) {
    switch (contactor) {
        case CONTACTOR_HV_POS:
            set_pin(AIR_POS_CLR_PORT, AIR_POS_CLR_PIN);
            vTaskDelay(1);
            set_pin(AIR_POS_SET_PORT, AIR_POS_SET_PIN);
            break;
        case CONTACTOR_HV_NEG:
            set_pin(AIR_NEG_CLR_PORT, AIR_NEG_CLR_PIN);
            vTaskDelay(1);
            set_pin(AIR_NEG_SET_PORT, AIR_NEG_SET_PIN);
            break;
        case CONTACTOR_HV_PRE:
            set_pin(AIR_PRE_CLR_PORT, AIR_PRE_CLR_PIN);
            vTaskDelay(1);
            set_pin(AIR_PRE_SET_PORT, AIR_PRE_SET_PIN);
            break;
    }
}

static inline void prv_open_all_contactors(void) {
    prv_open_contactor(CONTACTOR_HV_POS);
    prv_open_contactor(CONTACTOR_HV_NEG);
    prv_open_contactor(CONTACTOR_HV_PRE);
}

static void prv_standby(void) {
    prv_open_all_contactors();
    prvPrechargeTimeout = 0;
    prvStateMachineError = ERROR_NO_ERROR;
    PRINTF("State standby\n");
}

static void prv_pre_charge(void) {
    prv_open_contactor(CONTACTOR_HV_POS);
    prv_close_contactor(CONTACTOR_HV_PRE);
    prv_close_contactor(CONTACTOR_HV_NEG);
    PRINTF("State pre-charge\n");
}

static void prv_operate(void) {
    prv_close_contactor(CONTACTOR_HV_POS);
    prv_close_contactor(CONTACTOR_HV_NEG);
    prv_open_contactor(CONTACTOR_HV_PRE);
    PRINTF("State operate\n");
}

static void prv_error(void) {
    prv_open_all_contactors();
    PRINTF("State error\n");
    PRINTF("Reason %2x\n", prvStateMachineError);
}

void init_contactor(void) {
    prvStateMachine.current = CONTACTOR_STATE_STANDBY;
    xTaskCreate(prv_contactor_control_task, "contactor", CONTACTOR_TASK_STACK, NULL, CONTACTOR_TASK_PRIO, NULL);
    xTaskCreate(prv_system_state_task, "sysstate", SYSTEM_STATE_TASK_STACK, NULL, SYSTEM_STATE_TASK_PRIO, NULL);
}

static void prv_contactor_control_task(void *p) {
    (void) p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    xLastWakeTime = xTaskGetTickCount();

    init_wdt();

    while (1) {
        refresh_wdt();

        //Get intended and actual AIR states
        prvContactorState.negAIR_intent      = get_pin(AIR_NEG_INTENT_PORT, AIR_NEG_INTENT_PIN);
        prvContactorState.negAIR_actual      = get_pin(AIR_NEG_STATE_PORT,  AIR_NEG_STATE_PIN);
        prvContactorState.posAIR_intent      = get_pin(AIR_POS_INTENT_PORT, AIR_POS_INTENT_PIN);
        prvContactorState.posAIR_actual      = get_pin(AIR_POS_STATE_PORT,  AIR_POS_STATE_PIN);
        prvContactorState.pre_intent         = get_pin(AIR_PRE_INTENT_PORT, AIR_PRE_INTENT_PIN);
        prvContactorState.pre_actual         = get_pin(AIR_PRE_STATE_PORT,  AIR_PRE_STATE_PIN);

        prvContactorState.negAIR_isPlausible = prvContactorState.negAIR_intent == prvContactorState.negAIR_actual;
        prvContactorState.posAIR_isPlausible = prvContactorState.posAIR_intent == prvContactorState.posAIR_actual;
        prvContactorState.pre_isPlausible    = prvContactorState.pre_intent    == prvContactorState.pre_actual;

        //Poll external status pins
        prvAmsPowerStageDisabled = get_pin(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN);
        prvImdPowerStageDisabled = get_pin(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN);
        prvImdFault              = !get_pin(IMD_STAT_PORT, IMD_STAT_PIN);
        prvSdcOpen               = !get_pin(SC_STATUS_PORT, SC_STATUS_PIN);

        //AIR plausibility combined
        bool airPlausible = prvContactorState.negAIR_isPlausible && prvContactorState.posAIR_isPlausible && prvContactorState.pre_isPlausible;

        //Get battery and DC-Link voltage
        adc_data_t adcData;
        bool cpyret = copy_adc_data(&adcData, pdMS_TO_TICKS(200));

        if (cpyret == false) {
            prv_open_all_contactors();
            configASSERT(0);
        }

        /*
         * System is healthy if
         * 1) AMS and IMD are not in error state,
         * 2) AMS and IMD power stages are enabled to close the shutdown circuit,
         * 3) Rest of the shut down circuit is OK
         */
        bool systemIsHealthy = true;
        systemIsHealthy &= !prvAmsFault;
        systemIsHealthy &= !prvImdFault;
        systemIsHealthy &= !prvAmsPowerStageDisabled;
        systemIsHealthy &= !prvImdPowerStageDisabled;
        systemIsHealthy &= !prvSdcOpen;

        bool voltageEqual = false;


        prvStateMachineState = prvStateMachine.current;

        /* Pre-charge sequence:
         * DC-Link voltage has to reach at least 95% of the battery voltage */
        if (adcData.voltageValid && (fabs(adcData.batteryVoltage - adcData.dcLinkVoltage) <= (0.05f * adcData.batteryVoltage))) {
            voltageEqual = true;
        }

        /* Pre-charge timeout counter */
        if (prvStateMachine.current == CONTACTOR_STATE_PRE_CHARGE && !voltageEqual) {
            prvPrechargeTimeout++;
        }

        /* TS request is automatically withdrawn if the CAN message is absent for more than 500 ms */
        if (++prvTsRequestTimeout >= 5) {
            prvTsActive = false;
        }

        /* Initialize event */
        prvEvent = EVENT_NONE;

        /* Waiting for pre-charge and voltages are equal?
         * -> Pre-charge was successful */
        if ((prvStateMachine.current == CONTACTOR_STATE_PRE_CHARGE) && voltageEqual) {
            prvEvent = EVENT_PRE_CHARGE_SUCCESSFUL;
        }

        /* Waiting for pre-charge and TS activation request has been withdrawn?
         * -> Deactivate tractive system */
        if (prvStateMachine.current == CONTACTOR_STATE_PRE_CHARGE && !prvTsActive) {
            prvEvent = EVENT_TS_DEACTIVATE;
        }

        /* Pre-charge timed out?
         * -> Possibly a short-circuit at the TSAC output */
        if (prvStateMachine.current == CONTACTOR_STATE_PRE_CHARGE && prvPrechargeTimeout >= PRECHARGE_TIMEOUT) {
            prvEvent = EVENT_ERROR;
            prvStateMachineError |= ERROR_PRE_CHARGE_TIMEOUT;
        }

        /* Standby and TS activation has been requested?
         * -> Active tractive system with pre-charge sequence */
        if (prvStateMachine.current == CONTACTOR_STATE_STANDBY && prvTsActive) {
            prvEvent = EVENT_TS_ACTIVATE;
        }

        /* TS is active and activation request has been withdrawn?
         * -> Deactivate tractive system */
        if (prvStateMachine.current == CONTACTOR_STATE_OPERATE && !prvTsActive) {
            prvEvent = EVENT_TS_DEACTIVATE;
        }

        /* DC-Link voltage lower than 80% of the minimum battery voltage and contactors are active?
         * -> DC-Link voltage measurement broken wire */
        if (prvStateMachine.current == CONTACTOR_STATE_OPERATE && adcData.dcLinkVoltage < (0.8f * MIN_BATTERY_VOLTAGE)) {
            prvEvent = EVENT_ERROR;
            prvStateMachineError |= ERROR_IMPLAUSIBLE_DC_LINK_VOLTAGE;
        }

        /* Battery voltage lower than 80% of the minimum battery voltage and contactors are active?
         * -> Battery voltage measurement broken wire OR battery depleted OR main fuse blown :/ */
        if (prvStateMachine.current == CONTACTOR_STATE_OPERATE && adcData.batteryVoltage < (0.8f * MIN_BATTERY_VOLTAGE)) {
            prvEvent = EVENT_ERROR;
            prvStateMachineError |= ERROR_IMPLAUSIBLE_BATTERY_VOLTAGE;
        }

        /* Error state is active and TS request gets withdrawn?
         * -> This is considered as error reset. */
        if (prvStateMachine.current == CONTACTOR_STATE_ERROR && !prvTsActive) {
            prvEvent = EVENT_ERROR_CLEARED;
            prvStateMachineError = ERROR_NO_ERROR;
        }

        /* AMS or IMD report a fault?
         * -> Deactivate AIRs and report the current error states */
        if (!systemIsHealthy) {
            prvEvent = EVENT_ERROR;

            //Why is the system not healthy? Report the faults bit-wise
            if (prvAmsFault) {
                prvStateMachineError |= ERROR_AMS_FAULT;
            }

            if (prvImdFault) {
                prvStateMachineError |= ERROR_IMD_FAULT;
            }

            if (prvAmsPowerStageDisabled) {
                prvStateMachineError |= ERROR_AMS_POWERSTAGE_DISABLED;
            }

            if (prvImdPowerStageDisabled) {
                prvStateMachineError |= ERROR_IMD_POWERSTAGE_DISABLED;
            }

            if (prvSdcOpen) {
                prvStateMachineError |= ERROR_SDC_OPEN;
            }
        }

        /* AIR states are not plausible?
         * -> AIR might be stuck or state detection might be broken */
        if (!airPlausible) {
            prvEvent = EVENT_ERROR;
            prvStateMachineError |= ERROR_IMPLAUSIBLE_CONTACTOR;
        }

        for(size_t i = 0; i < sizeof(prvStateArray)/sizeof(prvStateArray[0]); i++) {
           if(prvStateArray[i].current == prvStateMachine.current) {
                if((prvStateArray[i].event == prvEvent)) {

                    prvStateMachine.current =  prvStateArray[i].next;

                    prvStateFunction[prvStateMachine.current].function();
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void prv_system_state_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);

    while (1) {

        stacks_data_t* stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            //Check for critical AMS values. This represents the AMS status.
            prvAmsFault = !stacksData->voltageValid;
            prvAmsFault |= !stacksData->temperatureValid;
            release_stacks_data();
        }

        adc_data_t *adcData = get_adc_data(portMAX_DELAY);
        if (adcData != NULL) {
            prvCurrentFault        = !adcData->currentValid;
            prvVoltageFault        = !adcData->voltageValid;
            prvCurrentOutOfRange   = false; //TODO implement a reasonable check taking the time and value into account. A short spike is not a problem, more than 5s of overload is
            release_adc_data();
        }

        if (prvAmsFault) {
            //AMS opens the shutdown circuit in case of critical values
            prv_open_shutdown_circuit();
            set_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
            clear_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
        } else {
            //If error clears, we close the shutdown circuit again
            prv_close_shutdown_circuit();
            clear_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
            if (!prvAmsPowerStageDisabled) {
                //LED is constantly on, if AMS error has been manually cleared
                set_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            } else {
                //A blinking green LED signalizes, that the AMS is ready but needs manual reset
                toggle_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            }
        }

        if (prvImdFault) {
            set_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);
            clear_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
        } else {
            clear_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);

            if (!prvImdPowerStageDisabled) {
                //LED is constantly on, if IMD error has been manually cleared
                set_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            } else {
                //A blinking green LED signalizes, that the IMD is ready but needs manual reset
                toggle_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void prv_open_shutdown_circuit(void) {
    clear_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

static void prv_close_shutdown_circuit(void) {
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

void request_tractive_system(bool active) {
    prvTsActive = active;
    if (active) {
        prvTsRequestTimeout = 0;
    }
}

contactor_SM_state_t get_contactor_SM_state(void) {
    return prvStateMachineState;
}

contactor_state_t get_contactor_state(void) {
    return prvContactorState;
}

contactor_error_t get_contactor_error(void) {
    return prvStateMachineError;
}

