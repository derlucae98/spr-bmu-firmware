/*
 * stacks.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "stacks.h"

static SemaphoreHandle_t prvStacksDataMutex = NULL;
static SemaphoreHandle_t prvBalancingGatesMutex = NULL;

static stacks_data_t prvStacksData;

static uint8_t prvBalancingGates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
static bool prvBalanceEnable = false;


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

static bool prv_ltc_mutex_take(TickType_t blocktime) {
    return spi_mutex_take(LTC6811_SPI, blocktime);
}

static void prv_ltc_mutex_give(void) {
    spi_mutex_give(LTC6811_SPI);
}

static uint16_t prv_max_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static uint16_t prv_min_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static uint16_t prv_avg_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
static bool prv_check_voltage_validity(uint8_t voltageStatus[][MAX_NUM_OF_CELLS+1], uint8_t stacks);
static bool prv_check_temperature_validity(uint8_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
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

    ltc6811_init(prv_ltc_mutex_take, prv_ltc_mutex_give, prv_ltc_spi, prv_ltc_assert, prv_ltc_deassert);

    uint32_t UID[MAX_NUM_OF_SLAVES];
    ltc6811_get_uid(UID);
    stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
    if (stacksData != NULL) {
        memcpy(stacksData->UID, UID, sizeof(UID));
        release_stacks_data();
    }

    xTaskCreate(stacks_worker_task, "LTC", LTC_WORKER_TASK_STACK, NULL, LTC_WORKER_TASK_PRIO, NULL);
    xTaskCreate(balancing_task, "balance", BALANCING_TASK_STACK, NULL, BALANCING_TASK_PRIO, NULL);
}

void stacks_worker_task(void *p) {
    (void)p;

    stacks_data_t stacksDataLocal;
    memset(&stacksDataLocal, 0, sizeof(stacks_data_t));
    static uint8_t pecVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(75);

    uint8_t cycle = 0;

    while (1) {

        ltc6811_wake_daisy_chain();
        ltc6811_set_balancing_gates(prvBalancingGates);
        ltc6811_get_voltage(stacksDataLocal.cellVoltage, pecVoltage);

        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
                stacksDataLocal.cellVoltage[slave][cell] /= 10; //chop off the 100 uV digit
            }
        }

        switch (cycle) {
        case 0:
            ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus, 0, 3);
            cycle = 1;
            break;
        case 1:
            ltc6811_open_wire_check(stacksDataLocal.cellVoltageStatus);
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
            for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
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

        bool cellVoltValid = prv_check_voltage_validity(stacksDataLocal.cellVoltageStatus, NUMBEROFSLAVES);
        stacksDataLocal.minCellVolt = prv_min_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.maxCellVolt = prv_max_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.avgCellVolt = prv_avg_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.voltageValid = cellVoltValid;

        bool cellTemperatureValid = prv_check_temperature_validity(stacksDataLocal.temperatureStatus, NUMBEROFSLAVES);
        stacksDataLocal.minTemperature = prv_min_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
        stacksDataLocal.maxTemperature = prv_max_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
        stacksDataLocal.avgTemperature = prv_avg_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
        stacksDataLocal.temperatureValid = cellTemperatureValid;

        stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            memcpy(stacksData, &stacksDataLocal, sizeof(stacks_data_t));
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
            bool valid = false;

            stacks_data_t *stacksData = get_stacks_data(portMAX_DELAY);
            if (stacksData != NULL) {
                memcpy(cellVoltage, stacksData->cellVoltage, sizeof(cellVoltage));
                minCellVoltage = stacksData->minCellVolt;
                maxCellVoltage = stacksData->maxCellVolt;
                valid = stacksData->voltageValid;
                release_stacks_data();
            }

            uint16_t delta = maxCellVoltage - minCellVoltage;

            if (!valid) {
                PRINTF("Balancing stopped due to invalid values!\n");
            }

            if (delta > 5 && valid) {
                //Balance only, if difference is greater than 5 mV
                for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
                    for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
                        if (cellVoltage[stack][cell] > (minCellVoltage + 5)) {
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

static bool prv_check_voltage_validity(uint8_t voltageStatus[][MAX_NUM_OF_CELLS+1], uint8_t stacks) {
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

static bool prv_check_temperature_validity(uint8_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t stacks) {
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
    return (uint16_t)(temp / ((MAX_NUM_OF_TEMPSENS - 2) * stacks));
}
