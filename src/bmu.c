/*
 * bmu.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "bmu.h"





static void can_send_task(void *p);
static void can_rec_task(void *p);




static uint16_t max_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
static uint16_t min_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
static uint16_t avg_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks);
static uint16_t max_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);
static uint16_t min_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);
static uint16_t avg_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks);

typedef struct {
    uint32_t UID[MAXSTACKS];
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    uint16_t temperature[MAXSTACKS][MAXTEMPSENS];
    uint8_t temperatureStatus[MAXSTACKS][MAXTEMPSENS];

    //BMS_Info_1
    uint16_t minCellVolt;
    bool minCellVoltValid;
    uint16_t maxCellVolt;
    bool maxCellVoltValid;
    uint16_t avgCellVolt;
    bool avgCellVoltValid;
    uint16_t minSoc;
    bool minSocValid;
    uint16_t maxSoc;
    bool maxSocValid;

    //BMS_Info_2
    uint16_t batteryVoltage;
    bool batteryVoltageValid;
    uint16_t dcLinkVoltage;
    bool dcLinkVoltageValid;
    int16_t current;
    bool currentValid;

    //BMS_Info_3
    uint16_t isolationResistance;
    bool isolationResistanceValid;
    bool shutdownStatus;
    uint8_t tsState;
    bool amsResetStatus;
    bool amsStatus;
    bool imdResetStatus;
    bool imdStatus;
    uint8_t errorCode;
    uint16_t minTemp;
    bool minTempValid;
    uint16_t maxTemp;
    bool maxTempValid;
    uint16_t avgTemp;
    bool avgTempValid;
} can_data_t;

void init_bmu(void) {
    init_sensors();
    init_contactor();

    init_safety();
    init_stacks();

    xTaskCreate(can_send_task, "CAN", 1000, NULL, 3, NULL);
    xTaskCreate(can_rec_task, "CAN rec", 300, NULL, 2, NULL);
    xTaskCreate(stacks_worker_task, "LTC", 2000, NULL, 3, NULL);
    xTaskCreate(safety_task, "status", 500, NULL, 3, NULL);
}

static void can_send_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();
    uint8_t counter = 0;
    can_msg_t msg;
    can_data_t canData;
    memset(&canData, 0, sizeof(can_data_t));

    while (1) {
        if (stacks_mutex_take(portMAX_DELAY)) {
            memcpy(canData.UID, stacksData.UID, sizeof(canData.UID));
            memcpy(canData.cellVoltage, stacksData.cellVoltage, sizeof(canData.cellVoltage));
            memcpy(canData.cellVoltageStatus, stacksData.cellVoltageStatus, sizeof(canData.cellVoltageStatus));
            memcpy(canData.temperature, stacksData.temperature, sizeof(canData.temperature));
            memcpy(canData.temperatureStatus, stacksData.temperatureStatus, sizeof(canData.temperatureStatus));

            bool cellVoltValid = check_voltage_validity(stacksData.cellVoltageStatus, NUMBEROFSLAVES);
            canData.minCellVolt = min_cell_voltage(stacksData.cellVoltage, NUMBEROFSLAVES);
            canData.minCellVoltValid = cellVoltValid;
            canData.maxCellVolt = max_cell_voltage(stacksData.cellVoltage, NUMBEROFSLAVES);
            canData.maxCellVoltValid = cellVoltValid;
            canData.avgCellVolt = avg_cell_voltage(stacksData.cellVoltage, NUMBEROFSLAVES);
            canData.avgCellVoltValid = cellVoltValid;

            canData.minSoc = (uint16_t)(stacksData.minSoc * 10);
            canData.minSocValid = stacksData.minSocValid;
            canData.maxSoc = (uint16_t)(stacksData.maxSoc * 10);
            canData.maxSocValid = stacksData.maxSocValid;

            bool cellTemperatureValid = check_temperature_validity(stacksData.temperatureStatus, NUMBEROFSLAVES);
            canData.minTemp = min_cell_temperature(stacksData.temperature, NUMBEROFSLAVES);
            canData.minTempValid = cellTemperatureValid;
            canData.maxTemp = max_cell_temperature(stacksData.temperature, NUMBEROFSLAVES);
            canData.maxTempValid = cellTemperatureValid;
            canData.avgTemp = avg_cell_temperature(stacksData.temperature, NUMBEROFSLAVES);
            canData.avgTempValid = cellTemperatureValid;
            stacks_mutex_give();
        }

        if (sensor_mutex_take(portMAX_DELAY)) {
            canData.current = (int16_t)(sensorData.current * 160);
            canData.currentValid = sensorData.currentValid;
            canData.batteryVoltage = (uint16_t)(sensorData.batteryVoltage * 10);
            canData.batteryVoltageValid = sensorData.batteryVoltageValid;
            canData.dcLinkVoltage = (uint16_t)(sensorData.dcLinkVoltage * 10);
            canData.dcLinkVoltageValid = sensorData.dcLinkVoltageValid;
            sensor_mutex_give();
        }

        if (batteryStatus_mutex_take(portMAX_DELAY)) {
            canData.isolationResistance = 0; //TODO isolation resistance CAN
            canData.isolationResistanceValid = false; //TODO isolation resistance CAN validity
            canData.shutdownStatus = batteryStatus.shutdownCircuit;
            canData.tsState = (uint8_t)contactorStateMachineState;
            canData.amsResetStatus = batteryStatus.amsResetStatus;
            canData.amsStatus = batteryStatus.amsStatus;
            canData.imdResetStatus = batteryStatus.imdResetStatus;
            canData.imdStatus = batteryStatus.imdStatus;
            canData.errorCode = (uint8_t)contactorStateMachineError;
            batteryStatus_mutex_give();
        }

        //BMS_Info_1
        msg.ID = 0x001;
        msg.DLC = 8;
        msg.payload[0] = canData.minCellVolt >> 5;
        msg.payload[1] = ((canData.minCellVolt & 0x1F) << 3) | ((canData.minCellVoltValid & 0x01) << 2) | ((canData.maxCellVolt & 0x1FFF) >> 11);
        msg.payload[2] = (canData.maxCellVolt & 0x7FF) >> 3;
        msg.payload[3] = ((canData.maxCellVolt & 0x07) << 5) | ((canData.maxCellVoltValid & 0x01) << 4) | ((canData.avgCellVolt & 0x1FFF) >> 9);
        msg.payload[4] = ((canData.avgCellVolt & 0x1FFF) >> 1);
        msg.payload[5] = ((canData.avgCellVolt & 0x1FFF) << 7) | ((canData.avgCellVoltValid & 0x01) << 6) | ((canData.minSoc & 0x3FF) >> 4);
        msg.payload[6] = ((canData.minSoc & 0x3FF) << 4) | ((canData.minSocValid & 0x01) << 3) | ((canData.maxSoc & 0x3FF) >> 7);
        msg.payload[7] = ((canData.maxSoc & 0x3FF) << 1) | ((canData.maxCellVoltValid & 0x01));
        can_send(CAN0, &msg);

        //BMS_Info_2
        msg.ID = 0x002;
        msg.DLC = 7;
        msg.payload[0] = ((canData.batteryVoltage) >> 5);
        msg.payload[1] = ((canData.batteryVoltage) << 3) | ((canData.batteryVoltageValid & 0x01) << 2);
        msg.payload[2] = ((canData.dcLinkVoltage & 0x1FFF) >> 5);
        msg.payload[3] = ((canData.dcLinkVoltage & 0x1FFF) << 3) | ((canData.dcLinkVoltageValid & 0x01) << 2);
        msg.payload[4] = (canData.current >> 8);
        msg.payload[5] = (canData.current & 0xFF);
        msg.payload[6] = (canData.currentValid & 0x01) << 7;
        can_send(CAN0, &msg);

        //BMS_Info_3
        msg.ID = 0x003;
        msg.DLC = 8;
        msg.payload[0] = (canData.isolationResistance & 0x7FFF) >> 8;
        msg.payload[1] = ((canData.isolationResistance & 0x7FFF) << 1) | ((canData.isolationResistanceValid & 0x01));
        msg.payload[2] = ((canData.shutdownStatus & 0x01) << 7) | ((canData.tsState & 0x03) << 5) | ((canData.amsResetStatus & 0x01) << 4)
                        | ((canData.amsStatus & 0x01) << 3) | ((canData.imdResetStatus & 0x01) << 2) | ((canData.imdStatus & 0x01) << 1);
        msg.payload[3] = ((canData.errorCode & 0x7F) << 1) | ((canData.minTemp & 0x3FF) >> 9);
        msg.payload[4] = (canData.minTemp & 0x3FF) >> 1;
        msg.payload[5] = ((canData.minTemp & 0x3FF) << 7) | ((canData.minTempValid & 0x01) << 6) | ((canData.maxTemp >> 4) & 0x3F);
        msg.payload[6] = ((canData.maxTemp & 0x3FF) << 4) | ((canData.maxTempValid & 0x01) << 3) | ((canData.avgTemp & 0x3FF) >> 7);
        msg.payload[7] = ((canData.avgTemp & 0x3FF) << 1) | (canData.avgTempValid & 0x01);
        can_send(CAN0, &msg);

        //Send BMS temperature 1 message every 10 ms
        msg.ID = 0x004;
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
        msg.ID = 0x005;
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
        msg.ID = 0x006;
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
        msg.ID = 0x007;
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
        msg.ID = 0x008;
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
        msg.ID = 0x009;
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
        msg.ID = 0x00A;
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
        msg.ID = 0x00B;
        msg.DLC = 5;
        msg.payload[0] = counter << 4;
        msg.payload[1] = stacksData.UID[counter] >> 24;
        msg.payload[2] = stacksData.UID[counter] >> 16;
        msg.payload[3] = stacksData.UID[counter] >> 8;
        msg.payload[4] = stacksData.UID[counter] & 0xFF;
        //Send message
        can_send(CAN0, &msg);

        if (counter < MAXSTACKS-1) {
            counter++;
        } else {
            counter = 0;
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static uint16_t max_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    uint16_t volt = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            if (voltage[stack][cell] > volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

static uint16_t min_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    uint16_t volt = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            if (voltage[stack][cell] < volt) {
                volt = voltage[stack][cell];
            }
        }
    }
    return volt;
}

static uint16_t avg_cell_voltage(uint16_t voltage[][MAXCELLS], uint8_t stacks) {
    double volt = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t cell = 0; cell < MAXCELLS; cell++) {
            volt += (double)voltage[stack][cell];
        }
    }
    return (uint16_t)(volt / (MAXCELLS * stacks));
}

static uint16_t max_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    uint16_t temp = 0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (temperature[stack][tempsens] > temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

static uint16_t min_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    uint16_t temp = -1;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            if (temperature[stack][tempsens] < temp) {
                temp = temperature[stack][tempsens];
            }
        }
    }
    return temp;
}

static uint16_t avg_cell_temperature(uint16_t temperature[][MAXTEMPSENS], uint8_t stacks) {
    double temp = 0.0;
    for (uint8_t stack = 0; stack < stacks; stack++) {
        for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
            temp += (double)temperature[stack][tempsens];
        }
    }
    return (uint16_t)(temp / (MAXTEMPSENS * stacks));
}

static void can_rec_task(void *p) {
    (void) p;
    can_msg_t msg;
    while (1) {
        configASSERT(BMU_Q_HANDLE);
        if (xQueueReceive(BMU_Q_HANDLE, &msg, portMAX_DELAY)) {
            switch (msg.ID) {
                case 0:
                    if (msg.DLC == 1 && msg.payload[0] == 0xFF) {
                        request_tractive_system(true);
                    } else {
                        request_tractive_system(false);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

