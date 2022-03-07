/*
 * stacks.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "stacks.h"

static SemaphoreHandle_t _stacksDataMutex = NULL;
stacks_data_t stacksData;
static uint8_t _balancingGates[NUMBEROFSLAVES][MAXCELLS];
static bool _balanceEnable = false;

static SemaphoreHandle_t _balancingGatesMutex = NULL;
static bool balancingGatesMutex_take(TickType_t blocktime);
static void balancingGatesMutex_give(void);

uint32_t stacksUID[MAXSTACKS];

void init_stacks(void) {
    _stacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(_stacksDataMutex);
    memset(&stacksData, 0, sizeof(stacks_data_t));

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
    const TickType_t xPeriod = pdMS_TO_TICKS(200);

    while (1) {

        dbg1_set();
        ltc6811_wake_daisy_chain();
        ltc6811_set_balancing_gates(_balancingGates);
        ltc6811_open_wire_check(stacksDataLocal.cellVoltageStatus);
        ltc6811_get_voltage(stacksDataLocal.cellVoltage, pecVoltage);
        ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus);

        // Error priority:
        // PEC error
        // Open cell wire
        // value out of range

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


        if (stacks_mutex_take(portMAX_DELAY)) {
            memcpy(&stacksData, &stacksDataLocal, sizeof(stacks_data_t));
            stacks_mutex_give();
        }
        dbg1_clear();



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
            uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
            uint16_t maxCellVoltage = 0;
            uint16_t minCellVoltage = 0;
            uint16_t avgCellVoltage = 0;
            bool valid = false;

            if (stacks_mutex_take(portMAX_DELAY)) {
                memcpy(cellVoltage, stacksData.cellVoltage, sizeof(cellVoltage));
                memcpy(cellVoltageStatus, stacksData.cellVoltageStatus, sizeof(cellVoltageStatus));
                minCellVoltage = stacksData.minCellVolt;
                maxCellVoltage = stacksData.maxCellVolt;
                avgCellVoltage = stacksData.avgCellVolt;
                valid = stacksData.voltageValid;
                stacks_mutex_give();
            }


            uint16_t delta = maxCellVoltage - minCellVoltage;
            if (delta > 5 && valid) {
                //Balance only, if difference is greater than 5 mV
                for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
                    for (size_t cell = 0; cell < MAXCELLS; cell++) {
                        if (cellVoltage[stack][cell] > (avgCellVoltage + 5)) {
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

void stacks_mutex_give(void) {
    xSemaphoreGive(_stacksDataMutex);
}

static bool balancingGatesMutex_take(TickType_t blocktime) {
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
            temp += (double)temperature[stack][tempsens];
        }
    }
    return (uint16_t)(temp / ((MAXTEMPSENS - 2) * stacks));
}
