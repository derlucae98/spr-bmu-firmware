/*
 * peripheral.c
 *
 *  Created on: Aug 24, 2023
 *      Author: luca
 */

#include "peripheral.h"

static SemaphoreHandle_t prvPeripheralMutex = NULL;

BaseType_t get_peripheral_mutex(TickType_t blocktime) {
    if (prvPeripheralMutex == NULL) {
        prvPeripheralMutex = xSemaphoreCreateMutex();
        configASSERT(prvPeripheralMutex);
    }

    return xSemaphoreTake(prvPeripheralMutex, blocktime);
}

void release_peripheral_mutex(void) {
    xSemaphoreGive(prvPeripheralMutex);
}
