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
    uint32_t UID[MAXSTACKS];
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    uint16_t temperature[MAXSTACKS][MAXTEMPSENS];
    uint8_t temperatureStatus[MAXSTACKS][MAXTEMPSENS];
    uint16_t packVoltage;
    float soc[MAXSTACKS][MAXCELLS];
    float minSoc;
    bool minSocValid;
    float maxSoc;
    bool maxSocValid;
} stacks_data_t;

void init_stacks(void);
void stacks_worker_task(void *p);
BaseType_t stacks_mutex_take(TickType_t blocktime);
void stacks_mutex_give(void);
extern stacks_data_t stacksData;


#endif /* STACKS_H_ */
