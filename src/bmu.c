/*
 * bmu.c
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#include "bmu.h"

static SemaphoreHandle_t _stacksDataMutex = NULL;
stacks_data_t stacksData;


static uint32_t _UID[NUMBEROFSLAVES];
static void can_send_task(void *p);
static void ltc6811_worker_task(void *p);

void init_bmu(void) {
    init_sensors();
    init_contactor();
    _stacksDataMutex = xSemaphoreCreateMutex();
    configASSERT(_stacksDataMutex);
    memset(&stacksData, 0, sizeof(stacks_data_t));
    ltc6811_init(_UID);
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        PRINTF("UID %d: %08X\n", slave+1, _UID[slave]);
    }
    //xTaskCreate(can_send_task, "CAN", 1000, NULL, 3, NULL);
    xTaskCreate(ltc6811_worker_task, "LTC", 2000, NULL, 3, NULL);
}

BaseType_t stacks_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_stacksDataMutex, blocktime);
}

void stacks_mutex_give(void) {
    xSemaphoreGive(_stacksDataMutex);
}

static void ltc6811_worker_task(void *p) {
    (void)p;
    stacks_data_t stacksDataLocal;
    static uint8_t pecVoltage[MAXSTACKS][MAXCELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        if (spi_mutex_take(LTC6811_SPI, portMAX_DELAY)) {



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
                    if ((stacksDataLocal.temperature[slave][tempsens] > MAXCELLTEMP) &&
                         stacksDataLocal.temperatureStatus[slave][tempsens] != PECERROR) {
                        stacksDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                    }

                    if ((stacksDataLocal.temperature[slave][tempsens] < 10) &&
                         stacksDataLocal.temperatureStatus[slave][tempsens] != PECERROR) {
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

            PRINTF("Cell 0: %u V\n", stacksDataLocal.cellVoltage[0][0]);

            if (stacks_mutex_take(portMAX_DELAY)) {
                memcpy(&stacksData, &stacksDataLocal, sizeof(stacks_data_t));
 //               memcpy(&stacksData.UID, _UID, sizeof(_UID));


                stacks_mutex_give();
            }





            spi_mutex_give(LTC6811_SPI);
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
        }
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

static void open_shutdown_circuit();
static void close_shutdown_circuit();
static volatile bool _criticalFault;

void shutdown_circuit_handle_task(void *p) {
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    _criticalFault = false;
    while (1) {
        if (stacks_mutex_take(portMAX_DELAY)) {
            // Check for critical values and open S/C
            _criticalFault = false;
            for (uint8_t stack = 0; stack < MAXSTACKS; stack++) {
                for (uint8_t cell = 0; cell < MAXCELLS+1; cell++) {
                    if (stacksData.cellVoltageStatus[stack][cell] != NOERROR) {
                        _criticalFault |= true;
                    }
                }
                for (uint8_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                    if (stacksData.temperatureStatus[stack][tempsens] != NOERROR) {
                        _criticalFault |= true;
                    }
                }
            }

//TODO fault handling



            stacks_mutex_give();
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

