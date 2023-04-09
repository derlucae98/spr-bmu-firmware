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
#include "config.h"
#include "LTC6811.h"
#include "spi.h"
#include <stdbool.h>

/*
 * NUMBEROFSLAVES can be different from this value.
 * This is useful during development when only one slave
 * is available for testing on the bench.
 * To ensure that the software will work with NUMBEROFSLAVES != 12,
 * MAX_NUM_OF_STACKS defines the number of slaves during normal operation.
 * If NUMBEROFSLAVES is less than this value, all unused values in the arrays
 * are filled with 0.
 * Note that a greater value for NUMBEROFSLAVES than MAX_NUM_OF_STACKS will lead
 * to crashes
 */
#define MAX_NUM_OF_STACKS 12

typedef struct {
    uint16_t cellVoltage[MAX_NUM_OF_STACKS][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_STACKS][MAX_NUM_OF_CELLS+1];
    uint16_t temperature[MAX_NUM_OF_STACKS][MAX_NUM_OF_TEMPSENS];
    uint8_t temperatureStatus[MAX_NUM_OF_STACKS][MAX_NUM_OF_TEMPSENS];

    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    bool voltageValid;
    uint16_t minTemperature;
    uint16_t maxTemperature;
    uint16_t avgTemperature;
    bool temperatureValid;

    uint32_t UID[MAX_NUM_OF_STACKS];
} stacks_data_t;

void init_stacks(void);
void control_balancing(bool enabled);
void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS]);

stacks_data_t* get_stacks_data(TickType_t blocktime);
bool copy_stacks_data(stacks_data_t *dest, TickType_t blocktime);
void release_stacks_data(void);


#endif /* STACKS_H_ */
