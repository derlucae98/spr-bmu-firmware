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
#include "spi.h"

#define MAX_NUM_OF_STACKS 12

typedef struct {
    uint16_t cellVoltage[MAX_NUM_OF_STACKS][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_STACKS][MAX_NUM_OF_CELLS+1];
    uint16_t temperature[MAX_NUM_OF_STACKS][MAX_NUM_OF_TEMPSENS];
    uint8_t temperatureStatus[MAX_NUM_OF_STACKS][MAX_NUM_OF_TEMPSENS];
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

extern uint32_t stacksUID[MAX_NUM_OF_STACKS];

uint16_t max_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
uint16_t min_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
uint16_t avg_cell_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], uint8_t stacks);
bool check_voltage_validity(uint8_t voltageStatus[][MAX_NUM_OF_CELLS+1], uint8_t stacks);
bool check_temperature_validity(uint8_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
uint16_t max_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
uint16_t min_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
uint16_t avg_cell_temperature(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);
void init_stacks(void);



stacks_data_t* get_stacks_data(TickType_t blocktime);
bool copy_stacks_data(stacks_data_t *dest, TickType_t blocktime);
void release_stacks_data(void);


void control_balancing(bool enabled);
void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS]);

#endif /* STACKS_H_ */
