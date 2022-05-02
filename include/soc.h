/*
 * soc.h
 *
 *  Created on: Apr 30, 2022
 *      Author: Luca Engelmann
 */

#ifndef SOC_H_
#define SOC_H_

#include <stdbool.h>
#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "sensors.h"
#include "stacks.h"
#include "eeprom.h"

typedef struct {
    float minSoc;
    float maxSoc;
    float avgSoc;
    bool valid;
} soc_stats_t;

typedef struct {
    float cellSoc[MAXSTACKS][MAXCELLS];
} cellSoc_t;

void init_soc(void);
bool soc_lookup(void);
cellSoc_t* get_soc(TickType_t blocktime);
bool copy_soc(cellSoc_t *dest, TickType_t blocktime);
void release_soc(void);
soc_stats_t get_soc_stats(void);





#endif /* SOC_H_ */
