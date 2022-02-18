/*
 * status.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "safety.h"

static SemaphoreHandle_t _batteryStatusMutex = NULL;
battery_status_t batteryStatus;

static inline void open_shutdown_circuit(void);
static inline void close_shutdown_circuit(void);


void init_safety(void) {
    _batteryStatusMutex = xSemaphoreCreateMutex();
    configASSERT(_batteryStatusMutex);
    memset(&batteryStatus, 0, sizeof(batteryStatus));
}

void safety_task(void *p) {
    /* This task polls all status inputs and determines the AMS status based on the measured cell parameters.
     * It controls the shutdown circuit for the AMS and also controls the status LEDs.
     */
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);

    while (1) {

        bool criticalValue = false;
        if (stacks_mutex_take(portMAX_DELAY)) {
            //Check for critical AMS values. This represents the AMS status.
            criticalValue |= !check_voltage_validity(stacksData.cellVoltageStatus, NUMBEROFSLAVES);
            criticalValue |= !check_temperature_validity(stacksData.temperatureStatus, NUMBEROFSLAVES);
            stacks_mutex_give();
        }

        if (sensor_mutex_take(portMAX_DELAY)) {
            criticalValue |= !sensorData.currentValid;
            criticalValue |= !sensorData.batteryVoltageValid;
            criticalValue |= !sensorData.dcLinkVoltageValid;
            sensor_mutex_give();
        }

        //Poll external status pins
        bool amsResetStatus = get_pin(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN);
        bool imdResetStatus = get_pin(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN);
        bool imdStatus = get_pin(IMD_STAT_PORT, IMD_STAT_PIN);
        bool scStat = get_pin(SC_STATUS_PORT, SC_STATUS_PIN);

        if (criticalValue) {
            //AMS opens the shutdown circuit in case of critical values
            open_shutdown_circuit();
            set_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
            clear_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
        } else {
            //If error clears, we close the shutdown circuit again
            close_shutdown_circuit();
            clear_pin(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN);
            if (amsResetStatus) {
                //LED is constantly on, if AMS error has been manually cleared
                set_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            } else {
                //A blinking green LED signalizes, that the AMS is ready but needs manual reset
                toggle_pin(LED_AMS_OK_PORT, LED_AMS_OK_PIN);
            }
        }

        if (imdStatus) {
            clear_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);
            if (imdResetStatus) {
                set_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            } else {
                toggle_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
            }
        } else {
            set_pin(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN);
            clear_pin(LED_IMD_OK_PORT, LED_IMD_OK_PIN);
        }

        if (batteryStatus_mutex_take(portMAX_DELAY)) {
            batteryStatus.amsStatus = !criticalValue;
            batteryStatus.amsResetStatus = amsResetStatus;
            batteryStatus.imdStatus = imdStatus;
            batteryStatus.imdResetStatus = imdResetStatus;
            batteryStatus.shutdownCircuit = scStat;
            // Get contactor states reported by TSAL
            batteryStatus.hvPosState = get_pin(HV_POS_STAT_PORT, HV_POS_STAT_PIN);
            batteryStatus.hvNegState = get_pin(HV_NEG_STAT_PORT, HV_NEG_STAT_PIN);
            batteryStatus.hvPreState = get_pin(HV_PRE_STAT_PORT, HV_PRE_STAT_PIN);
            batteryStatus_mutex_give();
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
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
            if (temperatureStatus[stack][tempsens] != NOERROR) {
                critical |= true;
            }
        }
    }
    return !critical;
}

BaseType_t batteryStatus_mutex_take(TickType_t blocktime) {
    if (!_batteryStatusMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(_batteryStatusMutex, blocktime);
}

void batteryStatus_mutex_give(void) {
    xSemaphoreGive(_batteryStatusMutex);
}

static inline void open_shutdown_circuit(void) {
    clear_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

static inline void close_shutdown_circuit(void) {
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

