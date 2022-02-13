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
volatile event_t contactorEvent = EVENT_NONE;

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
    PRINTF("State pre-charge\n");
}

static void operate(void) {
    close_contactor(CONTACTOR_HV_POS);
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

        // Process events and set arg
        // ...
        //PRINTF("State: %s\n", stateFunction[_stateMachine.current].name);


        for(size_t i = 0; i < sizeof(stateArray)/sizeof(stateArray[0]); i++) {
               if(stateArray[i].current == _stateMachine.current) {
                   if((stateArray[i].event == contactorEvent)) {

                       _stateMachine.current =  stateArray[i].next;

                       stateFunction[_stateMachine.current].function();
                   }
               }
           }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}


