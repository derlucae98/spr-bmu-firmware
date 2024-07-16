/*!
 * @file            contactor.c
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

#include "contactor.h"

static void prv_contactor_control_task(void *p);

static void prv_standby(void);
static void prv_pre_charge(void);
static void prv_operate(void);
static void prv_error(void);

static void prv_open_shutdown_circuit(void);
static void prv_close_shutdown_circuit(void);

static void prv_evaluate_system(void);
static void prv_check_for_errors(void);


static bool prvTsActive = false;
static uint8_t prvTsRequestTimeout = 0;
static uint8_t prvPrechargeTimeout = 0;
static contactor_SM_state_t prvStateMachineState = CONTACTOR_STATE_STANDBY;
static uint32_t prvStateMachineError = ERROR_NO_ERROR;
static contactor_state_t prvContactorState;
static adc_data_t prvAdcData;

typedef struct {
    bool amsFault : 1;
    bool imdFault : 1;
    bool voltageFault  : 1;
    bool currentFault : 1;
    bool currentOutOfRange : 1;
    bool amsPowerStageDisabled : 1;
    bool imdPowerStageDisabled : 1;
    bool sdcOpen : 1;
    bool airImplausible : 1;
    bool amsCellOutOfRange : 1;
    bool amsCellOpenWire : 1;
    bool amsTemperatureOutOfRange : 1;
    bool amsTemperatureOpenWire : 1;
    bool amsDaisychainError : 1;
    bool batteryVoltageImplausible : 1;
    bool dcLinkVoltageImplausible : 1;
    bool preChargeTimeout : 1;
} fault_types_t;

static fault_types_t prvFaultTypes;


static uint8_t prvNumberOfStacks = 12;
static bool prvVoltagePlausibilityCheckEnable = true;

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
    prvPrechargeTimeout = 0;
    PRINTF("State error\n");
    PRINTF("Reason 0x%4X\n", prvStateMachineError);
}

void init_contactor(void) {
    config_t *config = get_config(portMAX_DELAY);
    if (config != NULL) {
        prvNumberOfStacks = config->numberOfStacks;
        prvVoltagePlausibilityCheckEnable = config->voltagePlausibilityCheckEnable;
        release_config();
    }

    prvStateMachine.current = CONTACTOR_STATE_STANDBY;
    memset(&prvFaultTypes, 0, sizeof(fault_types_t));

    xTaskCreate(prv_contactor_control_task, "contactor", CONTACTOR_TASK_STACK, NULL, CONTACTOR_TASK_PRIO, NULL);
}

static void prv_contactor_control_task(void *p) {
    (void) p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(75);
    xLastWakeTime = xTaskGetTickCount();

    init_wdt();

    while (1) {

        refresh_wdt();

        memset(&prvFaultTypes, 0, sizeof(fault_types_t));

        //Get battery and DC-Link voltage
        bool cpyret = copy_adc_data(&prvAdcData, pdMS_TO_TICKS(200));

        if (cpyret == false) {
           prv_open_all_contactors();
           configASSERT(0);
        }

        prv_evaluate_system();


        bool voltageEqual = false;

        /* Initialize event */
        prvEvent = EVENT_NONE;
        prvStateMachineState = prvStateMachine.current;

        /* Pre-charge sequence:
         * DC-Link voltage has to reach at least 95% of the battery voltage */
        if (prvAdcData.voltageValid && (fabs(prvAdcData.batteryVoltage - prvAdcData.dcLinkVoltage) <= (0.05f * prvAdcData.batteryVoltage))) {
            voltageEqual = true;
        }

        /* Pre-charge timeout counter */
        if (prvStateMachine.current == CONTACTOR_STATE_PRE_CHARGE && !voltageEqual) {
            prvPrechargeTimeout++;
        }

        /* Pre-charge timed out?
         * -> Possibly a short-circuit at the TSAC output */
        if (prvPrechargeTimeout >= PRECHARGE_TIMEOUT) {
            prvFaultTypes.preChargeTimeout = true;
        }

        /* TS request is automatically withdrawn if the CAN message is absent for more than 500 ms */
        if (++prvTsRequestTimeout >= 5) {
            prvTsActive = false;
        }

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

        /* Error state is active and TS request gets withdrawn?
         * -> This is considered as error reset. */
        if (prvStateMachine.current == CONTACTOR_STATE_ERROR && !prvTsActive) {
            prvEvent = EVENT_ERROR_CLEARED;
        }

        prv_check_for_errors();

        if (prvStateMachineError) {
            prvEvent = EVENT_ERROR;
        }

        for (size_t i = 0; i < sizeof(prvStateArray) / sizeof(prvStateArray[0]); i++) {
           if (prvStateArray[i].current == prvStateMachine.current) {
                if ((prvStateArray[i].event == prvEvent)) {
                    prvStateMachine.current =  prvStateArray[i].next;
                    prvStateFunction[prvStateMachine.current].function();
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void prv_check_for_errors(void) {
    prvStateMachineError = ERROR_NO_ERROR;

    if (prvFaultTypes.preChargeTimeout) {
        prvStateMachineError |= ERROR_PRE_CHARGE_TIMEOUT;
    }

    /* Current sensor invalid? */
    if (prvFaultTypes.currentFault) {
        prvStateMachineError |= ERROR_IMPLAUSIBLE_CURRENT;
    }

    /* Current out of range for too long? */
    if (prvFaultTypes.currentOutOfRange) {
        prvStateMachineError |= ERROR_CURRENT_OUT_OF_RANGE;
    }

    /* HV voltage measurement invalid? */
    if (prvFaultTypes.voltageFault) {
        prvStateMachineError |= ERROR_IMPLAUSIBLE_BATTERY_VOLTAGE;
        prvStateMachineError |= ERROR_IMPLAUSIBLE_DC_LINK_VOLTAGE;
    }

    if (prvFaultTypes.amsCellOutOfRange) {
        prvStateMachineError |= ERROR_AMS_CELL_VOLTAGE_OUT_OF_RANGE;
    }

    if (prvFaultTypes.amsCellOpenWire) {
        prvStateMachineError |= ERROR_AMS_CELL_OPEN_WIRE;
    }

    if (prvFaultTypes.amsTemperatureOutOfRange) {
        prvStateMachineError |= ERROR_AMS_CELL_TEMPERATURE_OUT_OF_RANGE;
    }

    if (prvFaultTypes.amsTemperatureOpenWire) {
        prvStateMachineError |= ERROR_AMS_TEMPERATURE_OPEN_WIRE;
    }

    if (prvFaultTypes.amsDaisychainError) {
        prvStateMachineError |= ERROR_AMS_DAISYCHAIN_ERROR;
    }

    /* Insulation fault */
    if (prvFaultTypes.imdFault) {
        prvStateMachineError |= ERROR_IMD_FAULT;
    }

    /* AMS powerstage disabled */
    if (prvFaultTypes.amsPowerStageDisabled) {
        prvStateMachineError |= ERROR_AMS_POWERSTAGE_DISABLED;
    }

    /* IMD powerstage disabled */
    if (prvFaultTypes.imdPowerStageDisabled) {
        prvStateMachineError |= ERROR_IMD_POWERSTAGE_DISABLED;
    }

    /* SDC open (AMS, IMD or other) */
    if (prvFaultTypes.sdcOpen) {
        prvStateMachineError |= ERROR_SDC_OPEN;
    }

    /* AIR states are not plausible?
    * -> AIR might be stuck or state detection might be broken */
//    if (prvFaultTypes.airImplausible) {
//        prvStateMachineError |= ERROR_IMPLAUSIBLE_CONTACTOR;
//    }

    if (prvFaultTypes.batteryVoltageImplausible) {
        prvStateMachineError |= ERROR_IMPLAUSIBLE_BATTERY_VOLTAGE;
    }

    if (prvFaultTypes.dcLinkVoltageImplausible) {
        prvStateMachineError |= ERROR_IMPLAUSIBLE_DC_LINK_VOLTAGE;
    }

    /* General AMS fault (See individual fault bits for clarification) */
    if (prvFaultTypes.amsFault) {
        prvStateMachineError |= ERROR_AMS_FAULT;
    }
}

static void prv_evaluate_system(void) {
    static bool blinkEnable;
    static bool oneSecondElapsed = false;
    static uint8_t oneSecondCounter = 0;
    blinkEnable = !blinkEnable;

    if (oneSecondCounter < 10) {
        oneSecondCounter++;
        oneSecondElapsed = false;
    } else {
        oneSecondCounter = 0;
        oneSecondElapsed = true;
    }

    stacks_data_t stacksData;
    copy_stacks_data(&stacksData, portMAX_DELAY);


    // Evaluate Stack healthiness
    for (size_t stack = 0; stack < prvNumberOfStacks; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (stacksData.cellVoltageStatus[stack][cell] == PECERROR) {
                prvFaultTypes.amsDaisychainError |= true;
                prvFaultTypes.amsFault |= true;
            }
            if (stacksData.cellVoltageStatus[stack][cell] == VALUEOUTOFRANGE) {
                prvFaultTypes.amsCellOutOfRange |= true;
                prvFaultTypes.amsFault |= true;
            }
            if (stacksData.cellVoltageStatus[stack][cell] == OPENCELLWIRE) {
                prvFaultTypes.amsCellOpenWire |= true;
                prvFaultTypes.amsFault |= true;
            }
        }
        for (size_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            if (stacksData.temperatureStatus[stack][tempsens] == PECERROR) {
                prvFaultTypes.amsDaisychainError |= true;
                prvFaultTypes.amsFault |= true;
            }
            if (stacksData.temperatureStatus[stack][tempsens] == VALUEOUTOFRANGE) {
                prvFaultTypes.amsTemperatureOutOfRange |= true;
                prvFaultTypes.amsFault |= true;
            }
            if (stacksData.temperatureStatus[stack][tempsens] == OPENCELLWIRE) {
                prvFaultTypes.amsTemperatureOpenWire |= true;
                prvFaultTypes.amsFault |= true;
            }
        }
    }

    float current = 0.0f;
    static uint8_t currentTimeout = 0;

    // Evaluate ADC data healthiness
    adc_data_t *adcData = get_adc_data(portMAX_DELAY);
    if (adcData != NULL) {
        prvFaultTypes.currentFault = !adcData->currentValid;
        prvFaultTypes.voltageFault = !adcData->voltageValid;
        current = adcData->current;
        release_adc_data();
    }

    // Overcurrent detection
    if (!prvFaultTypes.currentFault && current > MAX_CURRENT) {
        currentTimeout++;
    } else {
        currentTimeout = 0;
    }

    if (currentTimeout >= MAX_CURRENT_TIME) {
        prvFaultTypes.currentOutOfRange = true;
    } else {
        prvFaultTypes.currentOutOfRange = false;
    }

    prvFaultTypes.amsFault |= prvFaultTypes.currentFault || prvFaultTypes.voltageFault || prvFaultTypes.currentOutOfRange;

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
    prvFaultTypes.airImplausible = false; //!(prvContactorState.negAIR_isPlausible && prvContactorState.posAIR_isPlausible && prvContactorState.pre_isPlausible);

    prvFaultTypes.amsFault |= prvFaultTypes.airImplausible;

    if (prvVoltagePlausibilityCheckEnable) {
        /* DC-Link voltage lower than 80% of the minimum battery voltage and contactors are active?
        * -> DC-Link voltage measurement broken wire */
        if (prvStateMachine.current == CONTACTOR_STATE_OPERATE && prvAdcData.dcLinkVoltage < (0.8f * (MIN_STACK_VOLTAGE / 10000) * prvNumberOfStacks)) {
            prvFaultTypes.dcLinkVoltageImplausible = true;
        }

        /* Battery voltage lower than 80% of the minimum battery voltage and contactors are active?
        * -> Battery voltage measurement broken wire OR battery depleted OR main fuse blown :/ */
        if (prvStateMachine.current == CONTACTOR_STATE_OPERATE && prvAdcData.batteryVoltage < (0.8f * (MIN_STACK_VOLTAGE / 10000) * prvNumberOfStacks)) {
            prvFaultTypes.batteryVoltageImplausible = true;
        }
    }

    prvFaultTypes.amsFault |= prvFaultTypes.dcLinkVoltageImplausible || prvFaultTypes.batteryVoltageImplausible;


    //Poll external status pins
    prvFaultTypes.amsPowerStageDisabled = get_pin(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN);
    prvFaultTypes.imdPowerStageDisabled = get_pin(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN);
    prvFaultTypes.imdFault              = !get_pin(IMD_STAT_PORT, IMD_STAT_PIN);
    prvFaultTypes.sdcOpen               = !get_pin(SC_STATUS_PORT, SC_STATUS_PIN);

    if (prvFaultTypes.amsFault) {
        //AMS opens the shutdown circuit in case of critical values
        prv_open_shutdown_circuit();
        set_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
        clear_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
    } else {
        //If error clears, we close the shutdown circuit again
        prv_close_shutdown_circuit();
        clear_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
        if (!prvFaultTypes.amsPowerStageDisabled) {
            //LED is constantly on, if AMS error has been manually cleared
            set_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
        } else {
            //A blinking green LED signalizes, that the AMS is ready but needs manual reset
            if (blinkEnable) {
                set_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            } else {
                clear_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            }
        }
    }

    if (prvFaultTypes.imdFault) {
        set_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);
        clear_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
    } else {
        clear_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);

        if (!prvFaultTypes.imdPowerStageDisabled) {
            //LED is constantly on, if IMD error has been manually cleared
            set_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
        } else {
            //A blinking green LED signalizes, that the IMD is ready but needs manual reset
            if (blinkEnable) {
                set_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            } else {
                clear_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            }
        }
    }

    //Warning LED: Slow blinking: No errors, fast blinking: Error
    if (prvStateMachineState == CONTACTOR_STATE_ERROR) {
        if (blinkEnable) {
            set_pin(LED_WARNING_PORT, LED_WARNING_PIN);
        } else {
            clear_pin(LED_WARNING_PORT, LED_WARNING_PIN);
        }
    } else {
        if (oneSecondElapsed) {
            toggle_pin(LED_WARNING_PORT, LED_WARNING_PIN);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    //Poll external status pins again after short delay to evaluate the new power stage and SDC states
    //This step is needed to accurately report the error reason
    prvFaultTypes.amsPowerStageDisabled = get_pin(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN);
    prvFaultTypes.imdPowerStageDisabled = get_pin(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN);
    prvFaultTypes.imdFault              = !get_pin(IMD_STAT_PORT, IMD_STAT_PIN);
    prvFaultTypes.sdcOpen               = !get_pin(SC_STATUS_PORT, SC_STATUS_PIN);

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

