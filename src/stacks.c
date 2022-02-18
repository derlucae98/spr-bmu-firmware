/*
 * stacks.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "stacks.h"

static SemaphoreHandle_t _stacksDataMutex = NULL;
stacks_data_t stacksData;

void init_stacks(void) {
    _stacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(_stacksDataMutex);
    memset(&stacksData, 0, sizeof(stacks_data_t));

    uint32_t UID[NUMBEROFSLAVES];
    ltc6811_init(UID);
    if (stacks_mutex_take(portMAX_DELAY)) {
        memcpy(&stacksData.UID, UID, sizeof(UID));
        stacks_mutex_give();
    }

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        PRINTF("UID %d: %08X\n", slave+1, UID[slave]);
    }
}

void stacks_worker_task(void *p) {
    (void)p;

    stacks_data_t stacksDataLocal;
    memset(&stacksDataLocal, 0, sizeof(stacks_data_t));
    static uint8_t pecVoltage[MAXSTACKS][MAXCELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(200);
    uint8_t balancingGates[NUMBEROFSLAVES][MAXCELLS];
    memset(balancingGates, 0, sizeof(balancingGates));

    while (1) {

        ltc6811_wake_daisy_chain();
        ltc6811_set_balancing_gates(balancingGates);
        ltc6811_open_wire_check(stacksDataLocal.cellVoltageStatus);
        ltc6811_get_voltage(stacksDataLocal.cellVoltage, pecVoltage);
        ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus);

        // Error priority:
        // PEC error
        // Open cell wire
        // value out of range


        //memset(&stackDataLocal.cellVoltageStatus, 0, sizeof(stackDataLocal.cellVoltageStatus));
        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            // Validity check for temperature sensors

            for (size_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                if (stacksDataLocal.temperatureStatus[slave][tempsens] == PECERROR) {
                    continue;
                }
                if (stacksDataLocal.temperature[slave][tempsens] > MAXCELLTEMP) {
                    stacksDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                } else if (stacksDataLocal.temperature[slave][tempsens] < 10) {
                     stacksDataLocal.temperatureStatus[slave][tempsens] = OPENCELLWIRE;
                }
            }

            // Validity checks for cell voltage measurement
            for (size_t cell = 0; cell < MAXCELLS; cell++) {
                // PEC error: highest prio
                if (pecVoltage[slave][cell] == PECERROR) {
                    stacksDataLocal.cellVoltageStatus[slave][cell + 1] = PECERROR;
                    continue;
                }
                // Open sensor wire: Prio 2
                if (stacksDataLocal.cellVoltageStatus[slave][cell + 1] == OPENCELLWIRE) {
                    continue;
                }
                // Value out of range: Prio 3
                if ((stacksDataLocal.cellVoltage[slave][cell] > CELL_OVERVOLTAGE)||
                    (stacksDataLocal.cellVoltage[slave][cell] < CELL_UNDERVOLTAGE)) {
                        stacksDataLocal.cellVoltageStatus[slave][cell + 1] = VALUEOUTOFRANGE;
                }

            }
        }

        if (stacks_mutex_take(portMAX_DELAY)) {
            memcpy(&stacksData, &stacksDataLocal, sizeof(stacks_data_t));
            stacks_mutex_give();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

BaseType_t stacks_mutex_take(TickType_t blocktime) {
    if (!_stacksDataMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(_stacksDataMutex, blocktime);
}

void stacks_mutex_give(void) {
    xSemaphoreGive(_stacksDataMutex);
}
