/*
 * bmu.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef BMU_H_
#define BMU_H_



#include "S32K146.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "contactor.h"
#include "LTC6811.h"
#include "config.h"
#include "can.h"
#include "sensors.h"





typedef struct {
    uint32_t UID[MAXSTACKS];
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    uint16_t temperature[MAXSTACKS][MAXTEMPSENS];
    uint8_t temperatureStatus[MAXSTACKS][MAXTEMPSENS];
    uint16_t packVoltage;
    float soc[MAXSTACKS][MAXCELLS];
    float minSoc;
    float maxSoc;
} stacks_data_t;

typedef struct {
    bool amsStatus;
    bool amsResetStatus;
    bool imdStatus;
    bool imdResetStatus;
    bool shutdownCircuit;
} battery_status_t;

BaseType_t stacks_mutex_take(TickType_t blocktime);
void stacks_mutex_give(void);
extern stacks_data_t stacksData;

BaseType_t batteryStatus_mutex_take(TickType_t blocktime);
void batteryStatus_mutex_give(void);
extern battery_status_t batteryStatus;



void init_bmu(void);




#endif /* BMU_H_ */
