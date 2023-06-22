/*
 * bmu.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "communication.h"

static void can_send_task(void *p);
static void can_rec_task(void *p);


typedef struct {
    uint32_t UID[MAX_NUM_OF_SLAVES];
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS+1];
    uint16_t temperature[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];
    uint8_t temperatureStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];

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

void init_comm(void) {
    xTaskCreate(can_send_task, "CAN", 1500, NULL, 3, NULL);
    xTaskCreate(can_rec_task, "CAN rec", 1500, NULL, 3, NULL);
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
    uint8_t balance[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    memset(balance, 0, sizeof(balance));

    while (1) {

        stacks_data_t* stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            memcpy(canData.UID, stacksData->UID, sizeof(stacksData->UID));
            memcpy(canData.cellVoltage, stacksData->cellVoltage, sizeof(canData.cellVoltage));
            memcpy(canData.cellVoltageStatus, stacksData->cellVoltageStatus, sizeof(canData.cellVoltageStatus));
            memcpy(canData.temperature, stacksData->temperature, sizeof(canData.temperature));
            memcpy(canData.temperatureStatus, stacksData->temperatureStatus, sizeof(canData.temperatureStatus));

            canData.minCellVolt = stacksData->minCellVolt;
            canData.minCellVoltValid = stacksData->voltageValid;
            canData.maxCellVolt = stacksData->maxCellVolt;
            canData.maxCellVoltValid = stacksData->voltageValid;
            canData.avgCellVolt = stacksData->avgCellVolt;
            canData.avgCellVoltValid = stacksData->voltageValid;

            canData.minTemp = stacksData->minTemperature;
            canData.minTempValid = stacksData->temperatureValid;
            canData.maxTemp = stacksData->maxTemperature;
            canData.maxTempValid = stacksData->temperatureValid;
            canData.avgTemp = stacksData->avgTemperature;
            canData.avgTempValid = stacksData->temperatureValid;

            release_stacks_data();
        }

        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
                canData.cellVoltage[slave][cell] /= 10; //chop off the 100 uV digit TODO: Remove with new CAN protocol
            }
        }
        canData.minCellVolt /= 10;
        canData.maxCellVolt /= 10;
        canData.avgCellVolt /= 10;

        adc_data_t *adcData = get_adc_data(portMAX_DELAY);
        if (adcData != NULL) {
            canData.batteryVoltage = (uint16_t)(adcData->batteryVoltage * 10);
            canData.batteryVoltageValid = adcData->voltageValid;
            canData.dcLinkVoltage = (uint16_t)(adcData->dcLinkVoltage * 10);
            canData.dcLinkVoltageValid = adcData->voltageValid;
            canData.current = (int16_t)(adcData->current * 160);
            canData.currentValid = adcData->currentValid;
            release_adc_data();
        }



        canData.isolationResistance = 0; //TODO isolation resistance CAN
        canData.isolationResistanceValid = false; //TODO isolation resistance CAN validity
        canData.shutdownStatus = 0;
        canData.tsState = 0;
        canData.amsResetStatus = 0;
        canData.amsStatus = 0;
        canData.imdResetStatus = 0;
        canData.imdStatus = 0;
        canData.errorCode = 0;



        canData.minSoc = 0;
        canData.maxSoc = 0;
        canData.minSocValid = 0;
        canData.maxSocValid = 0;

        if (counter == 0) {
            get_balancing_status(balance);
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
        msg.payload[7] = ((canData.maxSoc & 0x3FF) << 1) | ((canData.maxSocValid & 0x01));
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
        msg.payload[0] = (counter << 4) | ((canData.temperature[counter][0] >> 6) & 0x0F);
        msg.payload[1] = (canData.temperature[counter][0] << 2) | (canData.temperatureStatus[counter][0] & 0x03);
        msg.payload[2] = (canData.temperature[counter][1] >> 2);
        msg.payload[3] = (canData.temperature[counter][1] << 6) | ((canData.temperatureStatus[counter][1] & 0x03) << 4) | ((canData.temperature[counter][2] >> 6) & 0x0F);
        msg.payload[4] = (canData.temperature[counter][2] << 2) | (canData.temperatureStatus[counter][2] & 0x03);
        msg.payload[5] = (canData.temperature[counter][3] >> 2);
        msg.payload[6] = (canData.temperature[counter][3] << 6) | ((canData.temperatureStatus[counter][3] & 0x03) << 4) | ((canData.temperature[counter][4] >> 6) & 0x0F);
        msg.payload[7] = (canData.temperature[counter][4] << 2) | (canData.temperatureStatus[counter][4] & 0x03);
        //Send message
        can_send(CAN0, &msg);

        //Send BMS temperature 2 message every 10 ms
        msg.ID = 0x005;
        msg.DLC = 8;
        msg.payload[0] = (counter << 4) | ((canData.temperature[counter][5] >> 6) & 0x0F);
        msg.payload[1] = (canData.temperature[counter][5] << 2) | (canData.temperatureStatus[counter][5] & 0x03);
        msg.payload[2] = 0;
        msg.payload[3] = 0;
        msg.payload[4] = 0;
        msg.payload[5] = 0;
        msg.payload[6] = 0;
        msg.payload[7] = 0;
        //Send message
        can_send(CAN0, &msg);

        //Send BMS temperature 3 message every 10 ms
        msg.ID = 0x006;
        msg.DLC = 8;
        msg.payload[0] = (counter << 4);
        msg.payload[1] = 0;
        msg.payload[2] = 0;
        msg.payload[3] = 0;
        msg.payload[4] = 0;
        msg.payload[5] = 0;
        msg.payload[6] = 0;
        msg.payload[7] = 0;
        //Send message
        can_send(CAN0, &msg);

        //Send BMS cell voltage 1 message every 10 ms
        msg.ID = 0x007;
        msg.DLC = 7;
        msg.payload[0] = (counter << 4) | (canData.cellVoltageStatus[counter][0] & 0x3);
        msg.payload[1] = canData.cellVoltage[counter][0] >> 5;
        msg.payload[2] = ((canData.cellVoltage[counter][0] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][1] & 0x3);
        msg.payload[3] = canData.cellVoltage[counter][1] >> 5;
        msg.payload[4] = ((canData.cellVoltage[counter][1] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][2] & 0x3);
        msg.payload[5] = canData.cellVoltage[counter][2] >> 5;
        msg.payload[6] = ((canData.cellVoltage[counter][2] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][3] & 0x3);
        //Send message
        can_send(CAN0, &msg);

        //Send BMS cell voltage 2 message every 10 ms
        msg.ID = 0x008;
        msg.DLC = 7;
        msg.payload[0] = counter << 4;
        msg.payload[1] = canData.cellVoltage[counter][3] >> 5;
        msg.payload[2] = ((canData.cellVoltage[counter][3] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][4] & 0x3);
        msg.payload[3] = canData.cellVoltage[counter][4] >> 5;
        msg.payload[4] = ((canData.cellVoltage[counter][4] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][5] & 0x3);
        msg.payload[5] = canData.cellVoltage[counter][5] >> 5;
        msg.payload[6] = ((canData.cellVoltage[counter][5] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][6] & 0x3);
        //Send message
        can_send(CAN0, &msg);

        //Send BMS cell voltage 3 message every 10 ms
        msg.ID = 0x009;
        msg.DLC = 7;
        msg.payload[0] = counter << 4;
        msg.payload[1] = canData.cellVoltage[counter][6] >> 5;
        msg.payload[2] = ((canData.cellVoltage[counter][6] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][7] & 0x3);
        msg.payload[3] = canData.cellVoltage[counter][7] >> 5;
        msg.payload[4] = ((canData.cellVoltage[counter][7] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][8] & 0x3);
        msg.payload[5] = canData.cellVoltage[counter][8] >> 5;
        msg.payload[6] = ((canData.cellVoltage[counter][8] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][9] & 0x3);
        //Send message
        can_send(CAN0, &msg);

        //Send BMS cell voltage 4 message every 10 ms
        msg.ID = 0x00A;
        msg.DLC = 7;
        msg.payload[0] = counter << 4;
        msg.payload[1] = canData.cellVoltage[counter][9] >> 5;
        msg.payload[2] = ((canData.cellVoltage[counter][9] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][10] & 0x3);
        msg.payload[3] = canData.cellVoltage[counter][10] >> 5;
        msg.payload[4] = ((canData.cellVoltage[counter][10] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][11] & 0x3);
        msg.payload[5] = canData.cellVoltage[counter][11] >> 5;
        msg.payload[6] = ((canData.cellVoltage[counter][11] & 0x1F) << 3) | (canData.cellVoltageStatus[counter][12] & 0x3);
        //Send message
        can_send(CAN0, &msg);

        //Send Unique ID every 10 ms
        msg.ID = 0x00B;
        msg.DLC = 5;
        msg.payload[0] = counter << 4;
        msg.payload[1] = canData.UID[counter] >> 24;
        msg.payload[2] = canData.UID[counter] >> 16;
        msg.payload[3] = canData.UID[counter] >> 8;
        msg.payload[4] = canData.UID[counter] & 0xFF;
        //Send message
        can_send(CAN0, &msg);

        msg.ID = 0x00E;
        msg.payload[0] = counter;
        msg.payload[1] = ((balance[counter][11] & 0x01) << 7) | ((balance[counter][10] & 0x01) << 6) | ((balance[counter][9] & 0x01) << 5)
                | ((balance[counter][8] & 0x01) << 4) | ((balance[counter][7] & 0x01) << 3) | ((balance[counter][6] & 0x01) << 2)
                | ((balance[counter][5] & 0x01) << 1) | ((balance[counter][4] & 0x01));
        msg.payload[2] = ((balance[counter][3] & 0x01) << 7) | ((balance[counter][2] & 0x01) << 6) | ((balance[counter][1] & 0x01) << 5) | ((balance[counter][0] & 0x01) << 4);
        msg.DLC = 3;
        can_send(CAN0, &msg);

        msg.ID = 0x00F;
        msg.payload[0] = canData.minSoc >> 8;
        msg.payload[1] = canData.minSoc & 0xFF;
        msg.payload[2] = canData.maxTemp >> 8;
        msg.payload[3] = canData.maxTemp & 0xFF;
        msg.payload[4] = canData.tsState;
        msg.DLC = 5;
        can_send(CAN0, &msg);

        if (counter < MAX_NUM_OF_SLAVES-1) {
            counter++;
        } else {
            counter = 0;
        }


        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
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

                case CAN_ID_ISOTP_DOWN:
                    isotp_on_recv(&msg);
                    break;
                case CAN_ID_TEST:
                    isotp_send_test();
                    break;
                default:
                    break;
            }
        }
    }
}




