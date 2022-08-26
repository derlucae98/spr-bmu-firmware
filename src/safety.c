/*
 * status.c
 *
 *  Created on: Feb 18, 2022
 *      Author: Luca Engelmann
 */

#include "safety.h"

static SemaphoreHandle_t _batteryStatusMutex = NULL;
static battery_status_t _batteryStatus;

static inline void open_shutdown_circuit(void);
static inline void close_shutdown_circuit(void);

static BaseType_t batteryStatus_mutex_take(TickType_t blocktime);


void init_safety(void) {
    _batteryStatusMutex = xSemaphoreCreateMutex();
    configASSERT(_batteryStatusMutex);
    memset(&_batteryStatus, 0, sizeof(_batteryStatus));
}

void safety_task(void *p) {
    /* This task polls all status inputs and determines the AMS status based on the measured cell parameters.
     * It controls the shutdown circuit for the AMS and also controls the status LEDs.
     */
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);

    while (1) {

        volatile bool criticalValue = false;
        stacks_data_t* stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            //Check for critical AMS values. This represents the AMS status.
            criticalValue |= !check_voltage_validity(stacksData->cellVoltageStatus, NUMBEROFSLAVES);
            criticalValue |= !check_temperature_validity(stacksData->temperatureStatus, NUMBEROFSLAVES);
            release_stacks_data();
        }

        sensor_data_t *sensorData = get_sensor_data(portMAX_DELAY);
        if (sensorData != NULL) {
            criticalValue |= !sensorData->currentValid;
            criticalValue |= !sensorData->batteryVoltageValid;
            criticalValue |= !sensorData->dcLinkVoltageValid;
            release_sensor_data();
        }

        //Poll external status pins
        bool amsResetStatus = get_pin(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN);
        bool imdResetStatus = get_pin(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN);
        volatile bool imdStatus = get_pin(IMD_STAT_PORT, IMD_STAT_PIN);
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

//        if (criticalValue == false && imdStatus) {
////            printf("auto reset...\n");
//            set_pin(TP_5_PORT, TP_5_PIN);
//            vTaskDelay(pdMS_TO_TICKS(50));
//            clear_pin(TP_5_PORT, TP_5_PIN);
//        }

        battery_status_t *batteryStatus = get_battery_status(portMAX_DELAY);
        if (batteryStatus != NULL) {
            batteryStatus->amsStatus = !criticalValue;
            batteryStatus->amsResetStatus = amsResetStatus;
            batteryStatus->imdStatus = imdStatus;
            batteryStatus->imdResetStatus = imdResetStatus;
            batteryStatus->shutdownCircuit = scStat;
            // Get contactor states reported by TSAL
            batteryStatus->hvPosState = get_pin(HV_POS_STAT_PORT, HV_POS_STAT_PIN);
            batteryStatus->hvNegState = get_pin(HV_NEG_STAT_PORT, HV_NEG_STAT_PIN);
            batteryStatus->hvPreState = get_pin(HV_PRE_STAT_PORT, HV_PRE_STAT_PIN);
            release_battery_status();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}



BaseType_t batteryStatus_mutex_take(TickType_t blocktime) {
    if (!_batteryStatusMutex) {
        return pdFALSE;
    }
    return xSemaphoreTake(_batteryStatusMutex, blocktime);
}

void release_battery_status(void) {
    xSemaphoreGive(_batteryStatusMutex);
}

battery_status_t* get_battery_status(TickType_t blocktime) {
    if (batteryStatus_mutex_take(blocktime)) {
        return &_batteryStatus;
    } else {
        return NULL;
    }
}

bool copy_battery_status(battery_status_t *dest, TickType_t blocktime) {
    battery_status_t *src = get_battery_status(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(battery_status_t));
        release_battery_status();
        return true;
    } else {
        return false;
    }
}

static inline void open_shutdown_circuit(void) {
    clear_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

static inline void close_shutdown_circuit(void) {
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

