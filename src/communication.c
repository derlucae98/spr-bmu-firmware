/*
 * bmu.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "communication.h"

static void can_send_task(void *p);
static void can_rec_diag_task(void *p);
static void can_rec_vehic_task(void *p);
static void prv_timer_callback(TimerHandle_t timer);

static bool prvDiagTsControl = false;
static TimerHandle_t prvTsRequestTimeout = NULL;
static bool prvIsCharging = false;

typedef struct {
    //Info
    uint16_t isolationResistance;
    bool isolationResistanceValid;
    uint32_t errorCode;
    uint8_t tsState;

    //Stats1
    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    bool voltageValid;

    //Stats2
    uint16_t minSoc;
    uint16_t maxSoc;
    bool socValid;
    uint16_t dcLinkVoltage;
    bool dcLinkVoltageValid;
    uint8_t minTemp;
    uint8_t maxTemp;
    uint8_t avgTemp;
    bool tempValid;

    //UIP
    uint16_t batteryVoltage;
    bool batteryVoltageValid;
    int16_t current;
    bool currentValid;
    uint16_t batteryPower;
    bool batteryPowerValid;

    //Unique ID
    uint32_t UID[MAX_NUM_OF_SLAVES];

    //Cell voltage / temperature
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS+1];
    uint16_t temperature[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];
    uint8_t temperatureStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];

    //Balancing feedback
    uint8_t balance[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];

    //Time
    uint32_t uptime;
    uint32_t sysTime;
} can_data_t;

void init_comm(void) {
    xTaskCreate(can_send_task, "CAN", 1500, NULL, 3, NULL);
    xTaskCreate(can_rec_diag_task, "CAN rec", 1500, NULL, 3, NULL);
    xTaskCreate(can_rec_vehic_task, "CAN rec", 1500, NULL, 3, NULL);
    prvTsRequestTimeout = xTimerCreate("tstimeout", pdMS_TO_TICKS(500), pdFALSE, NULL, prv_timer_callback);
}

static void can_send_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();

    uint8_t slaveCounter = 0;
    uint8_t counter100ms = 0;
    can_msg_t msg;
    can_data_t canData;

    memset(&canData, 0, sizeof(can_data_t));

    //Send reset reason on startup over CAN
    uint32_t resetReason = get_reset_reason();
    msg.ID = CAN_ID_DIAG_STARTUP;
    msg.DLC = 4;
    memcpy(msg.payload, &resetReason, 4);
    can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

    while (1) {

        stacks_data_t* stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            memcpy(canData.UID, stacksData->UID, sizeof(canData.UID));
            memcpy(canData.cellVoltage, stacksData->cellVoltage, sizeof(canData.cellVoltage));
            memcpy(canData.cellVoltageStatus, stacksData->cellVoltageStatus, sizeof(canData.cellVoltageStatus));
            memcpy(canData.temperature, stacksData->temperature, sizeof(canData.temperature));
            memcpy(canData.temperatureStatus, stacksData->temperatureStatus, sizeof(canData.temperatureStatus));

            canData.minCellVolt = stacksData->minCellVolt;
            canData.maxCellVolt = stacksData->maxCellVolt;
            canData.avgCellVolt = stacksData->avgCellVolt;
            canData.voltageValid = stacksData->voltageValid;

            canData.minTemp = stacksData->minTemperature / 5;
            canData.maxTemp = stacksData->maxTemperature / 5;
            canData.avgTemp = stacksData->avgTemperature / 5;
            canData.tempValid = stacksData->temperatureValid;

            release_stacks_data();
        }

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

        canData.batteryPower = abs(canData.batteryVoltage * canData.current);
        canData.batteryPowerValid = canData.batteryVoltageValid && canData.currentValid;

        canData.isolationResistance = 0; //TODO isolation resistance CAN
        canData.isolationResistanceValid = false; //TODO isolation resistance CAN validity
        canData.errorCode = get_contactor_error();
        canData.tsState = get_contactor_SM_state();

        soc_stats_t socStats = get_soc_stats();
        canData.minSoc = (uint16_t)(socStats.minSoc + 0.5f);
        canData.maxSoc = (uint16_t)(socStats.maxSoc + 0.5f);
        canData.socValid = socStats.valid;

        if (counter100ms == 0) {
            canData.uptime = uptime_in_100_ms();
            canData.sysTime = rtc_get_unix_time();
            msg.ID = CAN_ID_DIAG_TIME;
            msg.DLC = 8;
            memcpy(msg.payload, &canData.uptime, sizeof(canData.uptime));
            memcpy(msg.payload + sizeof(canData.uptime), &canData.sysTime, sizeof(canData.sysTime));
            can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));
        }

        if (slaveCounter == 0) {
            get_balancing_status(canData.balance);
        }

        msg.ID = CAN_ID_DIAG_INFO;
        msg.DLC = 7;
        msg.payload[0] = (canData.errorCode >> 24) & 0xFF;
        msg.payload[1] = (canData.errorCode >> 16) & 0xFF;
        msg.payload[2] = (canData.errorCode >>  8) & 0xFF;
        msg.payload[3] = canData.errorCode & 0xFF;
        msg.payload[4] = (canData.isolationResistance >> 8) & 0xFF;
        msg.payload[5] = canData.isolationResistance & 0xFF;
        msg.payload[6] = ((canData.isolationResistanceValid & 0x01) << 7) | (canData.tsState & 0x0F);
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));
        msg.ID = CAN_ID_VEHIC_INFO;
        can_enqueue_message(CAN_VEHIC, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_STATS_1;
        msg.DLC = 7;
        msg.payload[0] = canData.voltageValid & 0x01;
        msg.payload[1] = (canData.minCellVolt >> 8) & 0xFF;
        msg.payload[2] = canData.minCellVolt & 0xFF;
        msg.payload[3] = (canData.maxCellVolt >> 8) & 0xFF;
        msg.payload[4] = canData.maxCellVolt & 0xFF;
        msg.payload[5] = (canData.avgCellVolt >> 8) & 0xFF;
        msg.payload[6] = canData.avgCellVolt & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));
        msg.ID = CAN_ID_VEHIC_STATS_1;
        can_enqueue_message(CAN_VEHIC, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_STATS_2;
        msg.DLC = 8;
        msg.payload[0] = ((canData.dcLinkVoltageValid & 0x01) << 2) | ((canData.socValid & 0x01) << 1) | (canData.tempValid & 0x01);
        msg.payload[1] = canData.minTemp;
        msg.payload[2] = canData.avgTemp; //Only for testing due to faulty sensor
        msg.payload[3] = canData.avgTemp;
        msg.payload[4] = canData.minSoc;
        msg.payload[5] = canData.maxSoc;
        msg.payload[6] = (canData.dcLinkVoltage >> 8) & 0xFF;
        msg.payload[7] = canData.dcLinkVoltage & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));
        msg.ID = CAN_ID_VEHIC_STATS_2;
        can_enqueue_message(CAN_VEHIC, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_UIP;
        msg.DLC = 7;
        msg.payload[0] = ((canData.batteryPowerValid & 0x01) << 2) | ((canData.currentValid & 0x01) << 1) | (canData.batteryVoltageValid & 0x01);
        msg.payload[1] = (canData.batteryVoltage >> 8) & 0xFF;
        msg.payload[2] = canData.batteryVoltage & 0xFF;
        msg.payload[3] = (canData.current >> 8) & 0xFF;
        msg.payload[4] = canData.current & 0xFF;
        msg.payload[5] = (canData.batteryPower >> 8) & 0xFF;
        msg.payload[6] = canData.batteryPower & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));
        msg.ID = CAN_ID_VEHIC_UIP;
        can_enqueue_message(CAN_VEHIC, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_CELL_VOLTAGE_1;
        msg.DLC = 8;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4) | (canData.cellVoltageStatus[slaveCounter][0] & 0x01);
        msg.payload[1] = ((canData.cellVoltageStatus[slaveCounter][3] & 0x03) << 4)
                       | ((canData.cellVoltageStatus[slaveCounter][2] & 0x03) << 2)
                       | (canData.cellVoltageStatus[slaveCounter][1] & 0x03);
        msg.payload[2] = (canData.cellVoltage[slaveCounter][0] >> 8) & 0xFF;
        msg.payload[3] = canData.cellVoltage[slaveCounter][0] & 0xFF;
        msg.payload[4] = (canData.cellVoltage[slaveCounter][1] >> 8) & 0xFF;
        msg.payload[5] = canData.cellVoltage[slaveCounter][1] & 0xFF;
        msg.payload[6] = (canData.cellVoltage[slaveCounter][2] >> 8) & 0xFF;
        msg.payload[7] = canData.cellVoltage[slaveCounter][2] & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_CELL_VOLTAGE_2;
        msg.DLC = 8;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4);
        msg.payload[1] = ((canData.cellVoltageStatus[slaveCounter][6] & 0x03) << 4)
                       | ((canData.cellVoltageStatus[slaveCounter][5] & 0x03) << 2)
                       | (canData.cellVoltageStatus[slaveCounter][4] & 0x03);
        msg.payload[2] = (canData.cellVoltage[slaveCounter][3] >> 8) & 0xFF;
        msg.payload[3] = canData.cellVoltage[slaveCounter][3] & 0xFF;
        msg.payload[4] = (canData.cellVoltage[slaveCounter][4] >> 8) & 0xFF;
        msg.payload[5] = canData.cellVoltage[slaveCounter][4] & 0xFF;
        msg.payload[6] = (canData.cellVoltage[slaveCounter][5] >> 8) & 0xFF;
        msg.payload[7] = canData.cellVoltage[slaveCounter][5] & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_CELL_VOLTAGE_3;
        msg.DLC = 8;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4);
        msg.payload[1] = ((canData.cellVoltageStatus[slaveCounter][9] & 0x03) << 4)
                       | ((canData.cellVoltageStatus[slaveCounter][8] & 0x03) << 2)
                       | (canData.cellVoltageStatus[slaveCounter][7] & 0x03);
        msg.payload[2] = (canData.cellVoltage[slaveCounter][6] >> 8) & 0xFF;
        msg.payload[3] = canData.cellVoltage[slaveCounter][6] & 0xFF;
        msg.payload[4] = (canData.cellVoltage[slaveCounter][7] >> 8) & 0xFF;
        msg.payload[5] = canData.cellVoltage[slaveCounter][7] & 0xFF;
        msg.payload[6] = (canData.cellVoltage[slaveCounter][8] >> 8) & 0xFF;
        msg.payload[7] = canData.cellVoltage[slaveCounter][8] & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_CELL_VOLTAGE_4;
        msg.DLC = 8;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4);
        msg.payload[1] = ((canData.cellVoltageStatus[slaveCounter][12] & 0x03) << 4)
                       | ((canData.cellVoltageStatus[slaveCounter][11] & 0x03) << 2)
                       | (canData.cellVoltageStatus[slaveCounter][10] & 0x03);
        msg.payload[2] = (canData.cellVoltage[slaveCounter][9] >> 8) & 0xFF;
        msg.payload[3] = canData.cellVoltage[slaveCounter][9] & 0xFF;
        msg.payload[4] = (canData.cellVoltage[slaveCounter][10] >> 8) & 0xFF;
        msg.payload[5] = canData.cellVoltage[slaveCounter][10] & 0xFF;
        msg.payload[6] = (canData.cellVoltage[slaveCounter][11] >> 8) & 0xFF;
        msg.payload[7] = canData.cellVoltage[slaveCounter][11] & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));


        for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
            for (size_t tempsens = 0; tempsens < MAX_NUM_OF_TEMPSENS; tempsens++) {
                canData.temperature[stack][tempsens] /= 5;
            }
        }

        msg.ID = CAN_ID_DIAG_CELL_TEMPERATURE;
        msg.DLC = 8;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4) | ((canData.temperatureStatus[slaveCounter][5] & 0x03) << 2)
                       | (canData.temperatureStatus[slaveCounter][4] & 0x03);
        msg.payload[1] = ((canData.temperatureStatus[slaveCounter][3] & 0x03) << 6) | ((canData.temperatureStatus[slaveCounter][2] & 0x03) << 4)
                       | ((canData.temperatureStatus[slaveCounter][1] & 0x03) << 2) | (canData.temperatureStatus[slaveCounter][0] & 0x03);
        msg.payload[2] = canData.temperature[slaveCounter][0];
        msg.payload[3] = canData.temperature[slaveCounter][1];
        msg.payload[4] = canData.temperature[slaveCounter][2];
        msg.payload[5] = canData.temperature[slaveCounter][3];
        msg.payload[6] = canData.temperature[slaveCounter][4];
        msg.payload[7] = canData.temperature[slaveCounter][5];
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_BALANCING_FEEDBACK;
        msg.DLC = 2;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4) | ((canData.balance[slaveCounter][11] & 0x01) << 3)
                       | ((canData.balance[slaveCounter][10] & 0x01) << 2) | ((canData.balance[slaveCounter][9] & 0x01) << 1)
                       | (canData.balance[slaveCounter][8] & 0x01);
        msg.payload[1] = ((canData.balance[slaveCounter][7] & 0x01) << 7) | ((canData.balance[slaveCounter][6] & 0x01) << 6)
                       | ((canData.balance[slaveCounter][5] & 0x01) << 5) | ((canData.balance[slaveCounter][4] & 0x01) << 4)
                       | ((canData.balance[slaveCounter][3] & 0x01) << 3) | ((canData.balance[slaveCounter][2] & 0x01) << 2)
                       | ((canData.balance[slaveCounter][1] & 0x01) << 1) | (canData.balance[slaveCounter][0] & 0x01);
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));

        msg.ID = CAN_ID_DIAG_UNIQUE_ID;
        msg.DLC = 5;
        msg.payload[0] = ((slaveCounter & 0x0F) << 4);
        msg.payload[1] = (canData.UID[slaveCounter] >> 24) & 0xFF;
        msg.payload[2] = (canData.UID[slaveCounter] >> 16) & 0xFF;
        msg.payload[3] = (canData.UID[slaveCounter] >> 8) & 0xFF;
        msg.payload[4] = canData.UID[slaveCounter] & 0xFF;
        can_enqueue_message(CAN_DIAG, &msg, pdMS_TO_TICKS(100));


        if (++counter100ms >= 10) {
            counter100ms = 0;
        }

        if (++slaveCounter >= MAX_NUM_OF_SLAVES) {
            slaveCounter = 0;
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

static void can_rec_vehic_task(void *p) {
    (void) p;
    can_msg_t msg;
    while (1) {
        if (xQueueReceive(CAN_VEHIC_RX_Q, &msg, portMAX_DELAY)) {
            switch (msg.ID) {
                case CAN_ID_VEHIC_TS_REQUEST:
                    if (msg.DLC == 1 && msg.payload[0] == 0xFF) {
                        if (!prvDiagTsControl) {
                            request_tractive_system(true);
                            prvIsCharging = false;
                        }
                    } else {
                        request_tractive_system(false);
                        prvIsCharging = false;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

static void can_rec_diag_task(void *p) {
    (void) p;
    can_msg_t msg;
    while (1) {
        if (xQueueReceive(CAN_DIAG_RX_Q, &msg, portMAX_DELAY)) {
            switch (msg.ID) {
                case CAN_ID_DIAG_TS_REQUEST:
                    if (msg.DLC != 2) {
                        continue;
                    }

                    if (msg.payload[0] & 0x01) {
                        prvDiagTsControl = true;
                        xTimerStart(prvTsRequestTimeout, portMAX_DELAY);
                    } else {
                        prvDiagTsControl = false;
                        xTimerStop(prvTsRequestTimeout, portMAX_DELAY);
                        request_tractive_system(false);
                    }

                    if (prvDiagTsControl) {
                        if (msg.payload[1] == 0xFF) {
                            request_tractive_system(true);
                            prvIsCharging = true;
                        } else {
                            request_tractive_system(false);
                            prvIsCharging = false;
                        }
                    }

                    break;

                case CAN_ID_DIAG_REQUEST:
                    handle_cal_request(msg.payload, msg.DLC);
                    break;
                default:
                    break;
            }
        }
    }
}

uint32_t get_reset_reason(void) {
    uint32_t resetReason = RCM->SRS;
    return resetReason;
}

void print_reset_reason(uint32_t resetReason) {
    if (resetReason & 0x0002) {
        PRINTF("Reset due to brown-out\n");
    } else if (resetReason & 0x0004) {
        PRINTF("Reset due to loss of clock\n");
    } else if (resetReason & 0x0008) {
        PRINTF("Reset due to loss of lock\n");
    } else if (resetReason & 0x0010) {
        PRINTF("Reset due to CMU loss of clock\n");
    } else if (resetReason & 0x0020) {
        PRINTF("Reset due to watchdog\n");
    } else if (resetReason & 0x0040) {
        PRINTF("Reset due to reset pin\n");
    } else if (resetReason & 0x0080) {
        PRINTF("Reset due to power cycle\n");
    } else if (resetReason & 0x0100) {
        PRINTF("Reset due to JTAG\n");
    } else if (resetReason & 0x0200) {
        PRINTF("Reset due to core lockup\n");
    } else if (resetReason & 0x0400) {
        PRINTF("Reset due to software\n");
    } else if (resetReason & 0x0800) {
        PRINTF("Reset due to host debugger\n");
    } else if (resetReason & 0x2000) {
        PRINTF("Reset due to stop ack error\n");
    }
}

static void prv_timer_callback(TimerHandle_t timer) {
    (void) timer;
    request_tractive_system(false);
    prvDiagTsControl = false;
}

bool system_is_charging(void) {
    return prvIsCharging;
}



