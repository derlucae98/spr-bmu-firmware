/*
 * contactor.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "contactor.h"

extern void PRINTF(const char *format, ...);

TaskHandle_t contactor_control_task_handle = NULL;
static void contactor_control_task(void *p);

static void standby(void);
static void pre_charge(void);
static void operate(void);
static void error(void);

static bool _tsActive = false;
static uint8_t _tsRequestTimeout = 0;
state_t contactorStateMachineState = STATE_STANDBY;
error_t contactorStateMachineError = ERROR_NO_ERROR;


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
            clear_pin(CONT_HV_POS_PORT, CONT_HV_POS_PIN);
            break;
        case CONTACTOR_HV_NEG:
            clear_pin(CONT_HV_NEG_PORT, CONT_HV_NEG_PIN);
            break;
        case CONTACTOR_HV_PRE:
            clear_pin(CONT_HV_PRE_PORT, CONT_HV_PRE_PIN);
            break;
    }
}

static inline void close_contactor(contactor_t contactor) {
    switch (contactor) {
        case CONTACTOR_HV_POS:
            set_pin(CONT_HV_POS_PORT, CONT_HV_POS_PIN);
            break;
        case CONTACTOR_HV_NEG:
            set_pin(CONT_HV_NEG_PORT, CONT_HV_NEG_PIN);
            break;
        case CONTACTOR_HV_PRE:
            set_pin(CONT_HV_PRE_PORT, CONT_HV_PRE_PIN);
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
    PRINTF("State standby\n");
}

static void pre_charge(void) {
    close_contactor(CONTACTOR_HV_NEG);
    close_contactor(CONTACTOR_HV_PRE);
    open_contactor(CONTACTOR_HV_POS);
    PRINTF("State pre-charge\n");
}

static void operate(void) {
    close_contactor(CONTACTOR_HV_POS);
    close_contactor(CONTACTOR_HV_NEG);
    open_contactor(CONTACTOR_HV_PRE);
    PRINTF("State operate\n");
}

static void error(void) {
    open_all_contactors();
    PRINTF("State error\n");
}

void init_contactor(void) {
    _stateMachine.current = STATE_STANDBY;
    xTaskCreate(contactor_control_task, "contactor", 500, NULL, 2, &contactor_control_task_handle);
}

static void contactor_control_task(void *p) {
    (void) p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    xLastWakeTime = xTaskGetTickCount();

    while (1) {

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

        contactorStateMachineState = _stateMachine.current;


        if (batteryStatus_mutex_take(portMAX_DELAY)) {
            systemIsHealthy &= batteryStatus.amsStatus;
            systemIsHealthy &= batteryStatus.amsResetStatus;
            systemIsHealthy &= batteryStatus.imdStatus;
            systemIsHealthy &= batteryStatus.imdResetStatus;
            systemIsHealthy &= batteryStatus.shutdownCircuit;

            contactorState = ((batteryStatus.hvPosState & 0x01) << 2) | ((batteryStatus.hvNegState & 0x01) << 1) | ((batteryStatus.hvPreState & 0x01));
            batteryStatus_mutex_give();
        }

        if (sensor_mutex_take(pdMS_TO_TICKS(portMAX_DELAY))) {
            if (fabs(sensorData.batteryVoltage - sensorData.dcLinkVoltage) <= (0.05f * sensorData.batteryVoltage)) {
                voltageEqual = true;
            }
            sensor_mutex_give();
        }

        switch (_stateMachine.current) {
        case STATE_STANDBY:
            // All contactors must be off
            if (contactorState != 0x0) {
                relayPlausible = false;
            }
            break;
        case STATE_PRE_CHARGE:
            // Only pre-charge and neg AIR active
            if (contactorState != 0x3) {
                relayPlausible = false;
            }
            break;
        case STATE_OPERATE:
            // Only pos AIR and neg AIR active
            if (contactorState != 0x6) {
                relayPlausible = false;
            }
            break;
        case STATE_ERROR:
            // All contactors must be off
            if (contactorState != 0x0) {
                relayPlausible = false;
            }
            break;
        }

        //TODO: Plausibility check is temporarily disabled. Cannot be tested with demonstrator.
        relayPlausible = true;



        if (++_tsRequestTimeout >= 5) {
            //timeout after 500ms
            _tsActive = false;
        }

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
            contactorStateMachineError = ERROR_NO_ERROR;
        }

        if (!systemIsHealthy) {
            _contactorEvent = EVENT_ERROR;
            contactorStateMachineError = ERROR_SYSTEM_NOT_HEALTHY;
        }
        if (!relayPlausible) {
            _contactorEvent = EVENT_ERROR;
            contactorStateMachineError = ERROR_CONTACTOR_IMPLAUSIBLE;
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


