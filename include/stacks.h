/*
 * stacks.h
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#ifndef STACKS_H_
#define STACKS_H_

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdbool.h>
#include "config.h"
#include "LTC6811.h"


typedef struct {
    uint16_t cellVoltage[NUMBEROFSLAVES][MAXCELLS];
    uint8_t cellVoltageStatus[NUMBEROFSLAVES][MAXCELLS+1];
    uint16_t temperature[NUMBEROFSLAVES][MAXTEMPSENS];
    uint8_t temperatureStatus[NUMBEROFSLAVES][MAXTEMPSENS];
    uint16_t packVoltage;
    float soc[NUMBEROFSLAVES][MAXCELLS];
    float minSoc;
    bool minSocValid;
    float maxSoc;
    bool maxSocValid;

    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    bool voltageValid;
    uint16_t minTemperature;
    uint16_t maxTemperature;
    uint16_t avgTemperature;
    bool temperatureValid;
} stacks_data_t;

extern uint32_t stacksUID[MAXSTACKS];

uint16_t max_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
uint16_t min_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
uint16_t avg_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
bool check_voltage_validity(uint8_t voltageStatus[][MAXCELLS+1], uint8_t stacks);
bool check_temperature_validity(uint8_t temperatureStatus[][MAXTEMPSENS], uint8_t stacks);
uint16_t max_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);
uint16_t min_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);
uint16_t avg_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);
void init_stacks(void);
void stacks_worker_task(void *p);
void balancing_task(void *p);
BaseType_t stacks_mutex_take(TickType_t blocktime);
void stacks_mutex_give(void);
extern stacks_data_t stacksData;

void control_balancing(bool enabled);
void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAXCELLS]);

#endif /* STACKS_H_ */
