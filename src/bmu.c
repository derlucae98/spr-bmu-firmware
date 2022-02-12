/*
 * bmu.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "bmu.h"

static SemaphoreHandle_t _stacksDataMutex = NULL;
stacks_data_t stacksData;
static SemaphoreHandle_t _batteryStatusMutex = NULL;
battery_status_t batteryStatus;


static uint32_t _UID[NUMBEROFSLAVES];
static void can_send_task(void *p);
static void ltc6811_worker_task(void *p);
static void safety_task(void *p);

static inline void open_shutdown_circuit(void);
static inline void close_shutdown_circuit(void);

void init_bmu(void) {
    init_sensors();
    init_contactor();

    _stacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(_stacksDataMutex);
    memset(&stacksData, 0, sizeof(stacks_data_t));

    _batteryStatusMutex = xSemaphoreCreateMutex();
    configASSERT(_batteryStatusMutex);
    memset(&batteryStatus, 0, sizeof(batteryStatus));

    ltc6811_init(_UID);
    if (stacks_mutex_take(portMAX_DELAY)) {
        memcpy(&stacksData.UID, _UID, sizeof(_UID));
        stacks_mutex_give();
    }

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        PRINTF("UID %d: %08X\n", slave+1, _UID[slave]);
    }
    xTaskCreate(can_send_task, "CAN", 1000, NULL, 3, NULL);
    xTaskCreate(ltc6811_worker_task, "LTC", 2000, NULL, 3, NULL);
    xTaskCreate(safety_task, "status", 500, NULL, 3, NULL);
}

BaseType_t stacks_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_stacksDataMutex, blocktime);
}

void stacks_mutex_give(void) {
    xSemaphoreGive(_stacksDataMutex);
}

BaseType_t batteryStatus_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_batteryStatusMutex, blocktime);
}

void batteryStatus_mutex_give(void) {
    xSemaphoreGive(_batteryStatusMutex);
}

static void ltc6811_worker_task(void *p) {
    (void)p;
    stacks_data_t stacksDataLocal;
    static uint8_t pecVoltage[MAXSTACKS][MAXCELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(200);
    while (1) {

        ltc6811_wake_daisy_chain();
        ltc6811_open_wire_check(stacksDataLocal.cellVoltageStatus);
        ltc6811_get_voltage(stacksDataLocal.cellVoltage, pecVoltage);
        ltc6811_get_temperatures_in_degC(stacksDataLocal.temperature, stacksDataLocal.temperatureStatus);

        // Error priority:
        // PEC error
        // Open cell wire
        // value out of range


        //memset(&stackDataLocal.cellVoltageStatus, 0, sizeof(stackDataLocal.cellVoltageStatus));
        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            // Validity check for temperature sensors

            for (size_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                if (stacksDataLocal.temperatureStatus[slave][tempsens] == PECERROR) {
                    continue;
                }
                if (stacksDataLocal.temperature[slave][tempsens] > MAXCELLTEMP) {
                    stacksDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                } else if (stacksDataLocal.temperature[slave][tempsens] < 10) {
                     stacksDataLocal.temperatureStatus[slave][tempsens] = OPENCELLWIRE;
                }
            }

            // Validity checks for cell voltage measurement
            for (size_t cell = 0; cell < MAXCELLS; cell++) {
                // PEC error: highest prio
                if (pecVoltage[slave][cell] == PECERROR) {
                    stacksDataLocal.cellVoltageStatus[slave][cell + 1] = PECERROR;
                    continue;
                }
                // Open sensor wire: Prio 2
                if (stacksDataLocal.cellVoltageStatus[slave][cell + 1] == OPENCELLWIRE) {
                    continue;
                }
                // Value out of range: Prio 3
                if ((stacksDataLocal.cellVoltage[slave][cell] > CELL_OVERVOLTAGE)||
                    (stacksDataLocal.cellVoltage[slave][cell] < CELL_UNDERVOLTAGE)) {
                        stacksDataLocal.cellVoltageStatus[slave][cell + 1] = VALUEOUTOFRANGE;
                }

            }
        }

        if (stacks_mutex_take(portMAX_DELAY)) {
            memcpy(&stacksData, &stacksDataLocal, sizeof(stacks_data_t));
            stacks_mutex_give();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void safety_task(void *p) {
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
            for (uint8_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
                for (uint8_t cell = 0; cell < MAXCELLS+1; cell++) {
                    if (stacksData.cellVoltageStatus[stack][cell] != NOERROR) {
                        criticalValue |= true;
                    }
                }
                for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                    if (stacksData.temperatureStatus[stack][tempsens] != NOERROR) {
                        criticalValue |= true;
                    }
                }
            }
            stacks_mutex_give();
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
            batteryStatus_mutex_give();
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void can_send_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();
    uint8_t counter = 0;
    can_msg_t msg;

    while (1) {

        if (stacks_mutex_take(portMAX_DELAY)) {


            //Send BMS info message every 10 ms
            msg.ID = 0x001;
            msg.DLC = 5;
//            msg.payload[0] = ((int16_t) (stacksData.current * 100)) >> 8;
//            msg.payload[1] = ((int16_t) (stacksData.current * 100)) & 0xFF;
            msg.payload[2] = (stacksData.packVoltage & 0x1FFF) >> 5;
//            msg.payload[3] = ((stacksData.packVoltage & 0x1F) << 3) | ((stacksData.shutdownStatus & 0x01) << 2) | ((stacksData.bmsStatus & 0x01) << 1);
            msg.payload[4] = stacksData.minSoc;
            //Send message
            can_send(CAN0, &msg);


            //Send BMS temperature 1 message every 10 ms
            msg.ID = 0x003;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((stacksData.temperature[counter][0] >> 6) & 0x0F);
            msg.payload[1] = (stacksData.temperature[counter][0] << 2) | (stacksData.temperatureStatus[counter][0] & 0x03);
            msg.payload[2] = (stacksData.temperature[counter][1] >> 2);
            msg.payload[3] = (stacksData.temperature[counter][1] << 6) | ((stacksData.temperatureStatus[counter][1] & 0x03) << 4) | ((stacksData.temperature[counter][2] >> 6) & 0x0F);
            msg.payload[4] = (stacksData.temperature[counter][2] << 2) | (stacksData.temperatureStatus[counter][2] & 0x03);
            msg.payload[5] = (stacksData.temperature[counter][3] >> 2);
            msg.payload[6] = (stacksData.temperature[counter][3] << 6) | ((stacksData.temperatureStatus[counter][3] & 0x03) << 4) | ((stacksData.temperature[counter][4] >> 6) & 0x0F);
            msg.payload[7] = (stacksData.temperature[counter][4] << 2) | (stacksData.temperatureStatus[counter][4] & 0x03);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS temperature 2 message every 10 ms
            msg.ID = 0x004;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((stacksData.temperature[counter][5] >> 6) & 0x0F);
            msg.payload[1] = (stacksData.temperature[counter][5] << 2) | (stacksData.temperatureStatus[counter][5] & 0x03);
            msg.payload[2] = (stacksData.temperature[counter][6] >> 2);
            msg.payload[3] = (stacksData.temperature[counter][6] << 6) | ((stacksData.temperatureStatus[counter][6] & 0x03) << 4) | ((stacksData.temperature[counter][7] >> 6) & 0x0F);
            msg.payload[4] = (stacksData.temperature[counter][7] << 2) | (stacksData.temperatureStatus[counter][7] & 0x03);
            msg.payload[5] = (stacksData.temperature[counter][8] >> 2);
            msg.payload[6] = (stacksData.temperature[counter][8] << 6) | ((stacksData.temperatureStatus[counter][8] & 0x03) << 4) | ((stacksData.temperature[counter][9] >> 6) & 0x0F);
            msg.payload[7] = (stacksData.temperature[counter][9] << 2) | (stacksData.temperatureStatus[counter][9] & 0x03);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS temperature 3 message every 10 ms
            msg.ID = 0x005;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((stacksData.temperature[counter][10] >> 6) & 0x0F);
            msg.payload[1] = (stacksData.temperature[counter][10] << 2) | (stacksData.temperatureStatus[counter][10] & 0x03);
            msg.payload[2] = (stacksData.temperature[counter][11] >> 2);
            msg.payload[3] = (stacksData.temperature[counter][11] << 6) | ((stacksData.temperatureStatus[counter][11] & 0x03) << 4) | ((stacksData.temperature[counter][12] >> 6) & 0x0F);
            msg.payload[4] = (stacksData.temperature[counter][12] << 2) | (stacksData.temperatureStatus[counter][12] & 0x03);
            msg.payload[5] = (stacksData.temperature[counter][13] >> 2);
            msg.payload[6] = (stacksData.temperature[counter][13] << 6) | ((stacksData.temperatureStatus[counter][13] & 0x03) << 4);
            msg.payload[7] = 0;
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 1 message every 10 ms
            msg.ID = 0x006;
            msg.DLC = 7;
            msg.payload[0] = (counter << 4) | (stacksData.cellVoltageStatus[counter][0] & 0x3);
            msg.payload[1] = stacksData.cellVoltage[counter][0] >> 5;
            msg.payload[2] = ((stacksData.cellVoltage[counter][0] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][1] & 0x3);
            msg.payload[3] = stacksData.cellVoltage[counter][1] >> 5;
            msg.payload[4] = ((stacksData.cellVoltage[counter][1] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][2] & 0x3);
            msg.payload[5] = stacksData.cellVoltage[counter][2] >> 5;
            msg.payload[6] = ((stacksData.cellVoltage[counter][2] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][3] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 2 message every 10 ms
            msg.ID = 0x007;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = stacksData.cellVoltage[counter][3] >> 5;
            msg.payload[2] = ((stacksData.cellVoltage[counter][3] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][4] & 0x3);
            msg.payload[3] = stacksData.cellVoltage[counter][4] >> 5;
            msg.payload[4] = ((stacksData.cellVoltage[counter][4] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][5] & 0x3);
            msg.payload[5] = stacksData.cellVoltage[counter][5] >> 5;
            msg.payload[6] = ((stacksData.cellVoltage[counter][5] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][6] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 3 message every 10 ms
            msg.ID = 0x008;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = stacksData.cellVoltage[counter][6] >> 5;
            msg.payload[2] = ((stacksData.cellVoltage[counter][6] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][7] & 0x3);
            msg.payload[3] = stacksData.cellVoltage[counter][7] >> 5;
            msg.payload[4] = ((stacksData.cellVoltage[counter][7] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][8] & 0x3);
            msg.payload[5] = stacksData.cellVoltage[counter][8] >> 5;
            msg.payload[6] = ((stacksData.cellVoltage[counter][8] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][9] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 4 message every 10 ms
            msg.ID = 0x009;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = stacksData.cellVoltage[counter][9] >> 5;
            msg.payload[2] = ((stacksData.cellVoltage[counter][9] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][10] & 0x3);
            msg.payload[3] = stacksData.cellVoltage[counter][10] >> 5;
            msg.payload[4] = ((stacksData.cellVoltage[counter][10] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][11] & 0x3);
            msg.payload[5] = stacksData.cellVoltage[counter][11] >> 5;
            msg.payload[6] = ((stacksData.cellVoltage[counter][11] & 0x1F) << 3) | (stacksData.cellVoltageStatus[counter][12] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send Unique ID every 10 ms
            msg.ID = 0x00A;
            msg.DLC = 5;
            msg.payload[0] = counter << 4;
            msg.payload[1] = stacksData.UID[counter] >> 24;
            msg.payload[2] = stacksData.UID[counter] >> 16;
            msg.payload[3] = stacksData.UID[counter] >> 8;
            msg.payload[4] = stacksData.UID[counter] & 0xFF;
            //Send message
            can_send(CAN0, &msg);

            if (counter < NUMBEROFSLAVES-1) {
                counter++;
            } else {
                counter = 0;
            }

            stacks_mutex_give();

        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}


static inline void open_shutdown_circuit(void) {
    clear_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

static inline void close_shutdown_circuit(void) {
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);
}

