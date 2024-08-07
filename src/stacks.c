/*!
 * @file            stacks.c
 * @brief           Module which acquires the stack values.
 *                  The values are provided via a Mutex protected global variable
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

#include "stacks.h"

static SemaphoreHandle_t prvStacksDataMutex = NULL;
static SemaphoreHandle_t prvBalancingGatesMutex = NULL;

static stacks_data_t prvStacksData;

static uint8_t prvBalancingGates[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
static bool prvBalanceEnable = false;
static uint16_t prvBalanceThreshold;
static TaskHandle_t prvBalanceTaskHandle = NULL;
static stacks_data_t prvStacksDataLocal;

static BaseType_t prv_balancingGatesMutex_take(TickType_t blocktime);
static void prv_balancingGatesMutex_give(void);

static BaseType_t prv_stacks_mutex_take(TickType_t blocktime);

static void stacks_worker_task(void *p);
static void balancing_task(void *p);

static void prv_ltc_spi(uint8_t *a, size_t len) {
    spi_move_array(LTC6811_SPI, a, len);
}

static void prv_ltc_assert(void) {
    clear_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
}

static void prv_ltc_deassert(void) {
    set_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
}


static uint16_t prv_max_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static uint16_t prv_min_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static uint16_t prv_avg_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static uint16_t prv_max_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
static uint16_t prv_min_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
static uint16_t prv_avg_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);

void init_stacks(void) {
    prvStacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(prvStacksDataMutex);
    memset(&prvStacksData, 0, sizeof(stacks_data_t));

    prvBalancingGatesMutex = xSemaphoreCreateMutex();
    configASSERT(prvBalancingGatesMutex);
    memset(prvBalancingGates, 0, sizeof(prvBalancingGates));
    memset(&prvStacksDataLocal, 0, sizeof(stacks_data_t));

    uint8_t numberOfStacks = MAX_NUM_OF_SLAVES;
    config_t* config  = get_config(pdMS_TO_TICKS(500));
    if (config != NULL) {
        numberOfStacks = config->numberOfStacks;
        prvBalanceEnable = config->globalBalancingEnable;
        prvBalanceThreshold = config->balancingThreshold;
        release_config();
    }

    ltc6811_init(prv_ltc_spi, prv_ltc_assert, prv_ltc_deassert, numberOfStacks);

    uint32_t UID[MAX_NUM_OF_SLAVES];
    ltc6811_get_uid(UID);
    stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
    if (stacksData != NULL) {
        memcpy(prvStacksDataLocal.UID, UID, sizeof(uint32_t) * numberOfStacks);
        release_stacks_data();
    }

    xTaskCreate(stacks_worker_task, "LTC", LTC_WORKER_TASK_STACK, NULL, LTC_WORKER_TASK_PRIO, NULL);
    xTaskCreate(balancing_task, "balance", BALANCING_TASK_STACK, NULL, BALANCING_TASK_PRIO, &prvBalanceTaskHandle);
}

void stacks_worker_task(void *p) {
    (void)p;

    static uint8_t pecVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);

    uint8_t cycle = 0;

    while (1) {
        ltc6811_wake_daisy_chain();
        ltc6811_set_balancing_gates(prvBalancingGates);
        ltc6811_get_voltage(prvStacksDataLocal.cellVoltage, pecVoltage);

        switch (cycle) {
        case 0:
            ltc6811_get_temperatures_in_degC(prvStacksDataLocal.temperature, prvStacksDataLocal.temperatureStatus, 0, 3);
            cycle = 1;
            break;
        case 1:
            ltc6811_open_wire_check(prvStacksDataLocal.cellVoltageStatus);
            cycle = 0;
            break;
        }

        // Error priority:
        // PEC error
        // Open cell wire
        // value out of range

        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            // Validity check for temperature sensors

            for (size_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
                if (prvStacksDataLocal.temperatureStatus[slave][tempsens] == PECERROR) {
                    continue;
                }

                uint16_t maxCellTemp;
                // Different maximum temperatures for charging and discharging
                if (system_is_charging()) {
                    maxCellTemp = MAXCELLTEMP_CHARGING;
                } else {
                    maxCellTemp = MAXCELLTEMP;
                }

                if (prvStacksDataLocal.temperature[slave][tempsens] > maxCellTemp) {
                    prvStacksDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                } else if (prvStacksDataLocal.temperature[slave][tempsens] < 10) {
                    prvStacksDataLocal.temperatureStatus[slave][tempsens] = OPENCELLWIRE;
                }
            }

            // Validity checks for cell voltage measurement
            for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
                // PEC error: highest prio
                if (pecVoltage[slave][cell] == PECERROR) {
                    prvStacksDataLocal.cellVoltageStatus[slave][cell + 1] = PECERROR;
                    continue;
                }
                // Open sensor wire: Prio 2
                if (prvStacksDataLocal.cellVoltageStatus[slave][cell + 1] == OPENCELLWIRE) {
                    continue;
                }
                // Value out of range: Prio 3
                if ((prvStacksDataLocal.cellVoltage[slave][cell] > CELL_OVERVOLTAGE) ||
                    (prvStacksDataLocal.cellVoltage[slave][cell] < CELL_UNDERVOLTAGE)) {
                    prvStacksDataLocal.cellVoltageStatus[slave][cell + 1] = VALUEOUTOFRANGE;
                }

            }
        }

        bool cellVoltValid = check_voltage_validity(prvStacksDataLocal.cellVoltageStatus, NUMBEROFSLAVES);
        prvStacksDataLocal.minCellVolt = prv_min_cell_voltage(prvStacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        prvStacksDataLocal.maxCellVolt = prv_max_cell_voltage(prvStacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        prvStacksDataLocal.avgCellVolt = prv_avg_cell_voltage(prvStacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        prvStacksDataLocal.voltageValid = cellVoltValid;

        bool cellTemperatureValid = check_temperature_validity(prvStacksDataLocal.temperatureStatus, NUMBEROFSLAVES);
        prvStacksDataLocal.minTemperature = prv_min_cell_temperature(prvStacksDataLocal.temperature, NUMBEROFSLAVES);
        prvStacksDataLocal.maxTemperature = prv_max_cell_temperature(prvStacksDataLocal.temperature, NUMBEROFSLAVES);
        prvStacksDataLocal.avgTemperature = prv_avg_cell_temperature(prvStacksDataLocal.temperature, NUMBEROFSLAVES);
        prvStacksDataLocal.temperatureValid = cellTemperatureValid;

        stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            memcpy(stacksData, &prvStacksDataLocal, sizeof(stacks_data_t));
            release_stacks_data();
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void balancing_task(void *p) {
    (void) p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1000);
    uint8_t balancingGates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
    memset(balancingGates, 0, sizeof(balancingGates));
    while(1) {

        if (prvBalanceEnable) {

            uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
            uint16_t maxCellVoltage = 0;
            uint16_t minCellVoltage = 0;
            uint16_t avgCellVoltage = 0;
            bool valid = false;

            stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
            if (stacksData != NULL) {
                memcpy(cellVoltage, stacksData->cellVoltage, sizeof(cellVoltage));
                minCellVoltage = stacksData->minCellVolt;
                maxCellVoltage = stacksData->maxCellVolt;
                avgCellVoltage = stacksData->avgCellVolt;
                valid = stacksData->voltageValid;
                release_stacks_data();
            }

            uint16_t delta = maxCellVoltage - minCellVoltage;

            /* The following requirements have to be met in order for the balancing to be active:
             * 1) The voltage delta between the highest and lowest cell musst be > 5 mV
             * 2) The cell voltage measurement must be valid
             * 3) The average cell voltage must be greater than the balancing threshold set by the user
             * 4) The balancing must be globally enabled
             * 5) The system must be in charging mode (Regenerative braking is not considered charging mode)
             * 6) The system must be in a healthy state. Balancing must be interrupted in case of any fault
             * 7) Balancing must be disabled when the system is in error state
             */

            bool isCharging = system_is_charging();
            bool deltaLargeEnough = delta > 50; // 100 uV resolution
            bool avgVoltGreaterThreshold = avgCellVoltage >= prvBalanceThreshold;
            bool systemHealthy = get_contactor_error() == ERROR_NO_ERROR;
            bool isNotInErrorState = get_contactor_SM_state() != CONTACTOR_STATE_ERROR;
            bool balancing = deltaLargeEnough && valid && avgVoltGreaterThreshold && prvBalanceEnable && isCharging && systemHealthy &&isNotInErrorState;

            if (balancing) {
                for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
                    for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
                        if (cellVoltage[stack][cell] > (minCellVoltage + 50)) {
                            balancingGates[stack][cell] = 1;
                        } else {
                            balancingGates[stack][cell] = 0;
                        }
                    }
                }
            } else {
                memset(balancingGates, 0, sizeof(balancingGates));
            }
        } else {
            memset(balancingGates, 0, sizeof(balancingGates));
        }

        if (prv_balancingGatesMutex_take(portMAX_DELAY)) {
            memcpy(prvBalancingGates, balancingGates, sizeof(balancingGates));
            prv_balancingGatesMutex_give();
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

BaseType_t prv_stacks_mutex_take(TickType_t blocktime) {
    if (!prvStacksDataMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(prvStacksDataMutex, blocktime);
}

void release_stacks_data(void) {
    xSemaphoreGive(prvStacksDataMutex);
}

stacks_data_t* get_stacks_data(TickType_t blocktime) {
    if (prv_stacks_mutex_take(blocktime)) {
        return &prvStacksData;
    } else {
        return NULL;
    }
}

bool copy_stacks_data(stacks_data_t *dest, TickType_t blocktime) {
    stacks_data_t *src = get_stacks_data(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(stacks_data_t));
        release_stacks_data();
        return true;
    } else {
        return false;
    }
}

static BaseType_t prv_balancingGatesMutex_take(TickType_t blocktime) {
    if (!prvBalancingGatesMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(prvBalancingGatesMutex, blocktime);
}

static void prv_balancingGatesMutex_give(void) {
    xSemaphoreGive(prvBalancingGatesMutex);
}

void control_balancing(bool enabled) {
    prvBalanceEnable = enabled;
}

void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS]) {
    if (prv_balancingGatesMutex_take(pdMS_TO_TICKS(portMAX_DELAY))) {
        memcpy(gates, prvBalancingGates, sizeof(prvBalancingGates));
        prv_balancingGatesMutex_give();
    }
}

static uint16_t prv_max_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    uint16_t volt = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (voltage[stack][cell] > volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

static uint16_t prv_min_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    uint16_t volt = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (voltage[stack][cell] < volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

static uint16_t prv_avg_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    double volt = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            volt += (double)voltage[stack][cell];
        }
    }
    return (uint16_t)(volt / (MAX_NUM_OF_CELLS * stacks));
}

bool check_voltage_validity(uint8_t voltageStatus[][MAX_NUM_OF_CELLS+1], uint8_t stacks) {
    bool critical = false;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAX_NUM_OF_CELLS+1; cell++) {
            if (voltageStatus[stack][cell] != NOERROR) {
                critical |= true;
            }
        }
    }
    return !critical;
}

bool check_temperature_validity(uint8_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t stacks) {
    bool critical = false;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            if (temperatureStatus[stack][tempsens] != NOERROR) {
                critical |= true;
            }
        }
    }
    return !critical;
}

static uint16_t prv_max_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks) {
    uint16_t temp = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            if (temperature[stack][tempsens] > temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

static uint16_t prv_min_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks) {
    uint16_t temp = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            if (temperature[stack][tempsens] < temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

static uint16_t prv_avg_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks) {
    double temp = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
            temp += (double)temperature[stack][tempsens];
        }
    }
    return (uint16_t)(temp / (MAX_NUM_OF_TEMPSENS * stacks));
}
