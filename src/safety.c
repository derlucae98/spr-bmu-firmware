/*
 * safety.c
 *
 *  Created on: 16.08.2021
 *      Author: Luca Engelmann
 */

#include "safety.h"

static void open_shutdown_circuit();
static void close_shutdown_circuit();
static volatile bool _criticalFault;

void shutdown_circuit_handle_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    _criticalFault = false;
    while (1) {
        if (batteryData_mutex_take(portMAX_DELAY)) {
            // Check for critical values and open S/C
            _criticalFault = false;
            for (uint8_t stack = 0; stack < MAXSTACKS; stack++) {
                for (uint8_t cell = 0; cell < MAXCELLS+1; cell++) {
                    if (batteryData.cellVoltageStatus[stack][cell] != NOERROR) {
                        _criticalFault |= true;
                    }
                }
                for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                    if (batteryData.temperatureStatus[stack][tempsens] != NOERROR) {
                        _criticalFault |= true;
                    }
                }
            }
            if (_criticalFault) {
                batteryData.bmsStatus = false;
                batteryData.shutdownStatus = false;
                //clear_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
                open_shutdown_circuit();
                //set_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);

            } else {

                //toggle_pin(LED_PORT, LED_OR_PIN);
                close_shutdown_circuit();

                if (0/*!get_pin(RESET_STATUS_PORT, RESET_STATUS_PIN)*/) {
                    // BMS ready waiting for clear error by user
                    // flash green LED

                    batteryData.bmsStatus = true;
                    batteryData.shutdownStatus = false;
                } else {

                    batteryData.bmsStatus = true;
                    batteryData.shutdownStatus = true;
                }
            }



            batteryData_mutex_give();
            vTaskDelayUntil(&xLastWakeTime, xPeriod);
        }
    }
}

void open_shutdown_circuit() {
    clear_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

void close_shutdown_circuit() {
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}
