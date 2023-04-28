/*
 * stacks.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "stacks.h"

static SemaphoreHandle_t _stacksDataMutex = NULL;
static stacks_data_t _stacksData;
static uint8_t _balancingGates[NUMBEROFSLAVES][MAXCELLS];
static bool _balanceEnable = false;

static SemaphoreHandle_t _balancingGatesMutex = NULL;
static BaseType_t balancingGatesMutex_take(TickType_t blocktime);
static void balancingGatesMutex_give(void);
static BaseType_t stacks_mutex_take(TickType_t blocktime);

uint32_t stacksUID[MAXSTACKS];

void init_stacks(void) {
    _stacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(_stacksDataMutex);
    memset(&_stacksData, 0, sizeof(stacks_data_t));

    _balancingGatesMutex = xSemaphoreCreateMutex();
    configASSERT(_balancingGatesMutex);
    memset(_balancingGates, 0, sizeof(_balancingGates));

    ltc6811_init(stacksUID);
}

void stacks_worker_task(void *p) {
    (void)p;

    stacks_data_t stacksDataLocal;
    memset(&stacksDataLocal, 0, sizeof(stacks_data_t));
    static uint8_t pecVoltage[MAXSTACKS][MAXCELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);

    uint8_t cycle = 0;

    while (1) {

        ltc6811_wake_daisy_chain();
        ltc6811_get_voltage(stacksDataLocal.cellVoltage, pecVoltage);
        ltc6811_set_balancing_gates(_balancingGates);

        switch (cycle) {
        case 0:
            ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus);
            cycle = 1;
            break;
        case 1:
            ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus);
            cycle = 2;
            break;
        case 2:
            ltc6811_open_wire_check(stacksDataLocal.cellVoltageStatus);
            cycle = 0;
            break;
        }

        // Error priority:
        // PEC error
        // Open cell wire
        // value out of range
        uint8_t errorCounter = 0;
        uint16_t temperatureFaulty[NUMBEROFSLAVES][MAXTEMPSENS];
        memset(&temperatureFaulty[0][0], 0, NUMBEROFSLAVES * MAXTEMPSENS);

        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            // Validity check for temperature sensors

            for (size_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                if (stacksDataLocal.temperatureStatus[slave][tempsens] == PECERROR) {
                    continue;
                }
                if (stacksDataLocal.temperature[slave][tempsens] > MAXCELLTEMP) {
                    stacksDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                    errorCounter++;
                    temperatureFaulty[slave][tempsens] = 1;
                } else if (stacksDataLocal.temperature[slave][tempsens] < 10) {
                     stacksDataLocal.temperatureStatus[slave][tempsens] = OPENCELLWIRE;
                     errorCounter++;
                     temperatureFaulty[slave][tempsens] = 1;
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

        if (errorCounter <= 5) {
            //Battery has one faulty temperature sensor. Ignore up to five sensor faults. Faulty sensors are ignored in the statistics
            for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
                memset(&stacksDataLocal.temperatureStatus[slave][0], NOERROR, MAXTEMPSENS);
                for (size_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                    if (temperatureFaulty[slave][tempsens] == 1) {
                        stacksDataLocal.temperature[slave][tempsens] = 0;
                    }
                }
            }
        }

        bool cellVoltValid = check_voltage_validity(stacksDataLocal.cellVoltageStatus, NUMBEROFSLAVES);
        stacksDataLocal.minCellVolt = min_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.maxCellVolt = max_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.avgCellVolt = avg_cell_voltage(stacksDataLocal.cellVoltage, NUMBEROFSLAVES);
        stacksDataLocal.voltageValid = cellVoltValid;

        bool cellTemperatureValid = check_temperature_validity(stacksDataLocal.temperatureStatus, NUMBEROFSLAVES);
        stacksDataLocal.minTemperature = min_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
        stacksDataLocal.maxTemperature = max_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
        stacksDataLocal.avgTemperature = avg_cell_temperature(stacksDataLocal.temperature, NUMBEROFSLAVES);
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
    uint8_t balancingGates[NUMBEROFSLAVES][MAXCELLS];
    memset(balancingGates, 0, sizeof(balancingGates));
    while(1) {

        if (_balanceEnable) {

            uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
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
                    for (size_t cell = 0; cell < MAXCELLS; cell++) {
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

        if (balancingGatesMutex_take(portMAX_DELAY)) {
            memcpy(_balancingGates, balancingGates, sizeof(balancingGates));
            balancingGatesMutex_give();
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

void release_stacks_data(void) {
    xSemaphoreGive(_stacksDataMutex);
}

stacks_data_t* get_stacks_data(TickType_t blocktime) {
    if (stacks_mutex_take(blocktime)) {
        return &_stacksData;
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

static BaseType_t balancingGatesMutex_take(TickType_t blocktime) {
    if (!_balancingGatesMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(_balancingGatesMutex, blocktime);
}

static void balancingGatesMutex_give(void) {
    xSemaphoreGive(_balancingGatesMutex);
}

void control_balancing(bool enabled) {
    _balanceEnable = enabled;
}

void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAXCELLS]) {
    if (balancingGatesMutex_take(pdMS_TO_TICKS(portMAX_DELAY))) {
        memcpy(gates, _balancingGates, sizeof(_balancingGates));
        balancingGatesMutex_give();
    }
}

uint16_t max_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    uint16_t volt = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            if (voltage[stack][cell] > volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

uint16_t min_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    uint16_t volt = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            if (voltage[stack][cell] < volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

uint16_t avg_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    double volt = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            volt += (double)voltage[stack][cell];
        }
    }
    return (uint16_t)(volt / (MAXCELLS * stacks));
}

bool check_voltage_validity(uint8_t voltageStatus[][MAXCELLS+1], uint8_t stacks) {
    bool critical = false;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS+1; cell++) {
            if (voltageStatus[stack][cell] != NOERROR) {
                critical |= true;
            }
        }
    }
    return !critical;
}

bool check_temperature_validity(uint8_t temperatureStatus[][MAXTEMPSENS], uint8_t stacks) {
    bool critical = false;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (tempsens == 6 || tempsens == 13) {
                continue; //Don't include PCB temperatures in statistics
            }
            if (temperatureStatus[stack][tempsens] != NOERROR) {
                critical |= true;
            }
        }
    }
    return !critical;
}

uint16_t max_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    uint16_t temp = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (tempsens == 6 || tempsens == 13) {
                continue; //Don't include PCB temperatures in statistics
            }
            if (temperature[stack][tempsens] == 0) {
                continue;
            }
            if (temperature[stack][tempsens] > temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

uint16_t min_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    uint16_t temp = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (tempsens == 6 || tempsens == 13) {
                continue; //Don't include PCB temperatures in statistics
            }
            if (temperature[stack][tempsens] == 0) {
                continue;
            }
            if (temperature[stack][tempsens] < temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

uint16_t avg_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    double temp = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (tempsens == 6 || tempsens == 13) {
                continue; //Don't include PCB temperatures in statistics
            }
            if (temperature[stack][tempsens] == 0) {
                continue;
            }
            temp += (double)temperature[stack][tempsens];
        }
    }
    return (uint16_t)(temp / ((MAXTEMPSENS - 2) * stacks));
}
