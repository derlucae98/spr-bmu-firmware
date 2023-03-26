/*
 * contactor.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "contactor.h"

extern void PRINTF(const char *format, ...);

static void contactor_control_task(void *p);

static void standby(void);
static void pre_charge(void);
static void operate(void);
static void error(void);

static bool _tsActive = false;
static uint8_t _tsRequestTimeout = 0;
static uint8_t _contactorStateMachineState = STATE_STANDBY;
static uint8_t _contactorStateMachineError = ERROR_NO_ERROR;


typedef struct {
    state_t current;
} state_machine_t;



typedef struct {
    state_t current;
    event_t event;
    state_t next;
} state_array_t;

static state_array_t stateArray[] = {
    {STATE_STANDBY,    EVENT_TS_ACTIVATE,           STATE_PRE_CHARGE},
    {STATE_STANDBY,    EVENT_ERROR,                 STATE_ERROR},
    {STATE_PRE_CHARGE, EVENT_TS_DEACTIVATE,         STATE_STANDBY},
    {STATE_PRE_CHARGE, EVENT_ERROR,                 STATE_ERROR},
    {STATE_PRE_CHARGE, EVENT_PRE_CHARGE_SUCCESSFUL, STATE_OPERATE},
    {STATE_OPERATE,    EVENT_ERROR,                 STATE_ERROR},
    {STATE_OPERATE,    EVENT_TS_DEACTIVATE,         STATE_STANDBY},
    {STATE_ERROR,      EVENT_ERROR_CLEARED,         STATE_STANDBY}
};

typedef struct {
    const char* name;
    void (*function)(void);
} state_function_t;

static state_function_t stateFunction[] = {
    {"STATE_STANDBY",    &standby},
    {"STATE_PRE_CHARGE", &pre_charge},
    {"STATE_OPERATE",    &operate},
    {"STATE_ERROR",      &error}
};

static state_machine_t _stateMachine;
static event_t _contactorEvent = EVENT_NONE;

typedef enum {
    CONTACTOR_HV_POS,
    CONTACTOR_HV_NEG,
    CONTACTOR_HV_PRE
} contactor_t;

static inline void open_contactor(contactor_t contactor) {
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

static inline void close_contactor(contactor_t contactor) {
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

static inline void open_all_contactors(void) {
    open_contactor(CONTACTOR_HV_POS);
    open_contactor(CONTACTOR_HV_NEG);
    open_contactor(CONTACTOR_HV_PRE);
}

static void standby(void) {
    open_all_contactors();
//    PRINTF("State standby\n");
}

static void pre_charge(void) {
    open_contactor(CONTACTOR_HV_POS);
    close_contactor(CONTACTOR_HV_PRE);
    close_contactor(CONTACTOR_HV_NEG);
//    PRINTF("State pre-charge\n");
}

static void operate(void) {
    close_contactor(CONTACTOR_HV_POS);
    close_contactor(CONTACTOR_HV_NEG);
    open_contactor(CONTACTOR_HV_PRE);
//    PRINTF("State operate\n");
}

static void error(void) {
    open_all_contactors();
//    PRINTF("State error\n");
}

void init_contactor(void) {
    _stateMachine.current = STATE_STANDBY;
    xTaskCreate(contactor_control_task, "contactor", CONTACTOR_TASK_STACK, NULL, CONTACTOR_TASK_PRIO, NULL);
}

static void contactor_control_task(void *p) {
    (void) p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    xLastWakeTime = xTaskGetTickCount();

    init_wdt();

    while (1) {
        refresh_wdt();
        /*
         * 1) System is healthy if AMS and IMD are not in error state,
         * 2) AMS and IMD power stages are enabled to close the shutdown circuit,
         * 3) Rest of the shut down circuit is OK
         * 4) Relay state is plausible
         */
        bool systemIsHealthy = true;
        bool relayPlausible = true;
        bool voltageEqual = false;

        uint8_t contactorState = 0; //bit-coded: 00000xyz, x=pos, y=neg, z=pre
        bool stateNegAir = false;
        bool statePosAir = false;
        bool statePre = false;

        _contactorStateMachineState = _stateMachine.current;


        adc_data_t *adcData = get_adc_data(portMAX_DELAY);
        if (adcData != NULL) {
            if (fabs(adcData->batteryVoltage - adcData->dcLinkVoltage) <= (0.05f * adcData->batteryVoltage)) {
                voltageEqual = true;
            }
            release_adc_data();
        }
//        if (++_tsRequestTimeout >= 5) {
//            //timeout after 500ms
//            _tsActive = false;
//        }

        _contactorEvent = EVENT_NONE;



        if ((_stateMachine.current == STATE_PRE_CHARGE) && voltageEqual) {
            _contactorEvent = EVENT_PRE_CHARGE_SUCCESSFUL;
        }
        if (_stateMachine.current == STATE_PRE_CHARGE && !_tsActive) {
            _contactorEvent = EVENT_TS_DEACTIVATE;
        }

        if (_stateMachine.current == STATE_STANDBY && _tsActive) {
            _contactorEvent = EVENT_TS_ACTIVATE;
        }

        if (_stateMachine.current == STATE_OPERATE && !_tsActive) {
            _contactorEvent = EVENT_TS_DEACTIVATE;
        }

        //Clear error if TS request clears and errors are gone
        if (_stateMachine.current == STATE_ERROR && !_tsActive) {
            _contactorEvent = EVENT_ERROR_CLEARED;
            _contactorStateMachineError = ERROR_NO_ERROR;
        }

        if (!systemIsHealthy) {
            _contactorEvent = EVENT_ERROR;
            _contactorStateMachineError = ERROR_SYSTEM_NOT_HEALTHY;
        }
        if (!relayPlausible) {
            _contactorEvent = EVENT_ERROR;
            _contactorStateMachineError = ERROR_CONTACTOR_IMPLAUSIBLE;
        }

        for(size_t i = 0; i < sizeof(stateArray)/sizeof(stateArray[0]); i++) {
           if(stateArray[i].current == _stateMachine.current) {
                if((stateArray[i].event == _contactorEvent)) {

                    _stateMachine.current =  stateArray[i].next;

                    stateFunction[_stateMachine.current].function();
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void request_tractive_system(bool active) {
    _tsActive = active;
    if (active) {
        _tsRequestTimeout = 0;
    }
}

state_t get_contactor_state(void) {
    return _contactorStateMachineState;
}
error_t get_contactor_error(void) {
    return _contactorStateMachineError;
}


