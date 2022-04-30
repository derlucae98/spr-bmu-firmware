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
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    uint16_t temperature[MAXSTACKS][MAXTEMPSENS];
    uint8_t temperatureStatus[MAXSTACKS][MAXTEMPSENS];
    uint16_t packVoltage;

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

stacks_data_t* get_stacks_data(TickType_t blocktime);
bool copy_stacks_data(stacks_data_t *dest, TickType_t blocktime);
void release_stacks_data(void);


void control_balancing(bool enabled);
void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAXCELLS]);

#endif /* STACKS_H_ */
