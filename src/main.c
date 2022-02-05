#include "FreeRTOS.h"
#include "task.h"
#include "S32K146.h"
#include "can.h"
#include "clock.h"
#include "gpio.h"
#include "gpio_def.h"
#include "spi.h"
#include "uart.h"
#include "LTC6811.h"
#include "safety.h"
#include "mcp356x.h"
#include "sensors.h"

#include <string.h>
#include <stdarg.h>
#include <alloca.h>
#include <stdlib.h>

#define NUMBEROFSLAVES 12


void wdog_disable (void)
{
  WDOG->CNT = 0xD928C520;
  WDOG->TOVAL = 0x0000FFFF;
  WDOG->CS = 0x00002100;
}

static void uart_rec(char* s) {

    char *strtokHelp = NULL;
    const char *delim = " \r\n";
    char *token = NULL;
    token = strtok_r(s, delim, &strtokHelp);
    char *tokens[4];
    uint8_t counter = 0;
    while (token != NULL) {
        size_t len = strlen(token) + 1;
        tokens[counter] = alloca(len);
        strcpy(tokens[counter], token);
        counter++;
        token = strtok_r(NULL, delim, &strtokHelp);
    }

    if (strcmp(tokens[0], "cal") == 0) {
        if (strcmp(tokens[1], "curr") == 0) {
            if (strcmp(tokens[2], "gain") == 0) {
                sscanf(tokens[3], "%f", &currGain);
                PRINTF("calibrating gain for %s: %.3f\n", tokens[1], currGain);
            } else if (strcmp(tokens[2], "offset") == 0) {
                sscanf(tokens[3], "%f", &currOffset);
                PRINTF("calibrating offset for %s: %.3f\n", tokens[1], currOffset);
            }
        }
    }
    if (strcmp(tokens[0], "ref") == 0) {
        sscanf(tokens[1], "%f", &currRef);
        PRINTF("Ref: %.3f\n", tokens[1], currRef);
    }


}

void led_blink_task(void *pvParameters) {
	(void) pvParameters;



    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(1000);
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        toggle_pin(LED_WARNING_PORT, LED_WARNING_PIN);

		vTaskDelayUntil(&xLastWakeTime, xPeriod);
	}
}



void can_send_task(void *p) {
	(void)p;
	TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();
    uint8_t counter = 0;
    can_msg_t msg;

    while (1) {
        msg.ID = 0x001;
        msg.DLC = 5;
        msg.payload[0] = 0xAA;
        msg.payload[1] = 0xBB;
        msg.payload[2] = 0xCC;
        msg.payload[3] = 0xDD;
        msg.payload[4] = 0xEE;
        can_send(CAN0, &msg);
/*
        if (batteryData_mutex_take(portMAX_DELAY)) {


     		//Send BMS info message every 10 ms
            msg.ID = 0x001;
            msg.DLC = 5;
    		msg.payload[0] = batteryData.current >> 8;
    		msg.payload[1] = batteryData.current & 0xFF;
    		msg.payload[2] = (batteryData.packVoltage & 0x1FFF) >> 5;
    		msg.payload[3] = ((batteryData.packVoltage & 0x1F) << 3) | ((batteryData.shutdownStatus & 0x01) << 2) | ((batteryData.bmsStatus & 0x01) << 1);
    		msg.payload[4] = batteryData.soc;
    		//Send message
    		can_send(CAN0, &msg);


            //Send BMS temperature 1 message every 10 ms
            msg.ID = 0x003;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((batteryData.temperature[counter][0] >> 6) & 0x0F);
            msg.payload[1] = (batteryData.temperature[counter][0] << 2) | (batteryData.temperatureStatus[counter][0] & 0x03);
            msg.payload[2] = (batteryData.temperature[counter][1] >> 2);
            msg.payload[3] = (batteryData.temperature[counter][1] << 6) | ((batteryData.temperatureStatus[counter][1] & 0x03) << 4) | ((batteryData.temperature[counter][2] >> 6) & 0x0F);
            msg.payload[4] = (batteryData.temperature[counter][2] << 2) | (batteryData.temperatureStatus[counter][2] & 0x03);
            msg.payload[5] = (batteryData.temperature[counter][3] >> 2);
            msg.payload[6] = (batteryData.temperature[counter][3] << 6) | ((batteryData.temperatureStatus[counter][3] & 0x03) << 4) | ((batteryData.temperature[counter][4] >> 6) & 0x0F);
            msg.payload[7] = (batteryData.temperature[counter][4] << 2) | (batteryData.temperatureStatus[counter][4] & 0x03);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS temperature 2 message every 10 ms
            msg.ID = 0x004;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((batteryData.temperature[counter][5] >> 6) & 0x0F);
            msg.payload[1] = (batteryData.temperature[counter][5] << 2) | (batteryData.temperatureStatus[counter][5] & 0x03);
            msg.payload[2] = (batteryData.temperature[counter][6] >> 2);
            msg.payload[3] = (batteryData.temperature[counter][6] << 6) | ((batteryData.temperatureStatus[counter][6] & 0x03) << 4) | ((batteryData.temperature[counter][7] >> 6) & 0x0F);
            msg.payload[4] = (batteryData.temperature[counter][7] << 2) | (batteryData.temperatureStatus[counter][7] & 0x03);
            msg.payload[5] = (batteryData.temperature[counter][8] >> 2);
            msg.payload[6] = (batteryData.temperature[counter][8] << 6) | ((batteryData.temperatureStatus[counter][8] & 0x03) << 4) | ((batteryData.temperature[counter][9] >> 6) & 0x0F);
            msg.payload[7] = (batteryData.temperature[counter][9] << 2) | (batteryData.temperatureStatus[counter][9] & 0x03);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS temperature 3 message every 10 ms
            msg.ID = 0x005;
            msg.DLC = 8;
            msg.payload[0] = (counter << 4) | ((batteryData.temperature[counter][10] >> 6) & 0x0F);
            msg.payload[1] = (batteryData.temperature[counter][10] << 2) | (batteryData.temperatureStatus[counter][10] & 0x03);
            msg.payload[2] = (batteryData.temperature[counter][11] >> 2);
            msg.payload[3] = (batteryData.temperature[counter][11] << 6) | ((batteryData.temperatureStatus[counter][11] & 0x03) << 4) | ((batteryData.temperature[counter][12] >> 6) & 0x0F);
            msg.payload[4] = (batteryData.temperature[counter][12] << 2) | (batteryData.temperatureStatus[counter][12] & 0x03);
            msg.payload[5] = (batteryData.temperature[counter][13] >> 2);
            msg.payload[6] = (batteryData.temperature[counter][13] << 6) | ((batteryData.temperatureStatus[counter][13] & 0x03) << 4);
            msg.payload[7] = 0;
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 1 message every 10 ms
            msg.ID = 0x006;
            msg.DLC = 7;
            msg.payload[0] = (counter << 4) | (batteryData.cellVoltageStatus[counter][0] & 0x3);
            msg.payload[1] = batteryData.cellVoltage[counter][0] >> 5;
            msg.payload[2] = ((batteryData.cellVoltage[counter][0] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][1] & 0x3);
            msg.payload[3] = batteryData.cellVoltage[counter][1] >> 5;
            msg.payload[4] = ((batteryData.cellVoltage[counter][1] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][2] & 0x3);
            msg.payload[5] = batteryData.cellVoltage[counter][2] >> 5;
            msg.payload[6] = ((batteryData.cellVoltage[counter][2] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][3] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 2 message every 10 ms
            msg.ID = 0x007;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = batteryData.cellVoltage[counter][3] >> 5;
            msg.payload[2] = ((batteryData.cellVoltage[counter][3] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][4] & 0x3);
            msg.payload[3] = batteryData.cellVoltage[counter][4] >> 5;
            msg.payload[4] = ((batteryData.cellVoltage[counter][4] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][5] & 0x3);
            msg.payload[5] = batteryData.cellVoltage[counter][5] >> 5;
            msg.payload[6] = ((batteryData.cellVoltage[counter][5] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][6] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 3 message every 10 ms
            msg.ID = 0x008;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = batteryData.cellVoltage[counter][6] >> 5;
            msg.payload[2] = ((batteryData.cellVoltage[counter][6] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][7] & 0x3);
            msg.payload[3] = batteryData.cellVoltage[counter][7] >> 5;
            msg.payload[4] = ((batteryData.cellVoltage[counter][7] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][8] & 0x3);
            msg.payload[5] = batteryData.cellVoltage[counter][8] >> 5;
            msg.payload[6] = ((batteryData.cellVoltage[counter][8] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][9] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send BMS cell voltage 4 message every 10 ms
            msg.ID = 0x009;
            msg.DLC = 7;
            msg.payload[0] = counter << 4;
            msg.payload[1] = batteryData.cellVoltage[counter][9] >> 5;
            msg.payload[2] = ((batteryData.cellVoltage[counter][9] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][10] & 0x3);
            msg.payload[3] = batteryData.cellVoltage[counter][10] >> 5;
            msg.payload[4] = ((batteryData.cellVoltage[counter][10] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][11] & 0x3);
            msg.payload[5] = batteryData.cellVoltage[counter][11] >> 5;
            msg.payload[6] = ((batteryData.cellVoltage[counter][11] & 0x1F) << 3) | (batteryData.cellVoltageStatus[counter][12] & 0x3);
            //Send message
            can_send(CAN0, &msg);

            //Send Unique ID every 10 ms
            msg.ID = 0x00A;
            msg.DLC = 5;
            msg.payload[0] = counter << 4;
            msg.payload[1] = batteryData.UID[counter] >> 24;
            msg.payload[2] = batteryData.UID[counter] >> 16;
            msg.payload[3] = batteryData.UID[counter] >> 8;
            msg.payload[4] = batteryData.UID[counter] & 0xFF;
            //Send message
            can_send(CAN0, &msg);

            if (counter < NUMBEROFSLAVES-1) {
                counter++;
            } else {
                counter = 0;
            }

            batteryData_mutex_give();

        }
*/
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
	}
}

void gpio_init(void) {
    //Enable clocks for GPIO modules
    PCC->PCCn[PCC_PORTA_INDEX] = PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTB_INDEX] = PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTC_INDEX] = PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTD_INDEX] = PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTE_INDEX] = PCC_PCCn_CGC_MASK;

    set_direction(LED_CARD_PORT, LED_CARD_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_CARD_PORT, LED_CARD_PIN, DRIVE_STRENGTH_HIGH);
    set_direction(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN, DRIVE_STRENGTH_HIGH);
    set_direction(LED_IMD_OK_PORT, LED_IMD_OK_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_IMD_OK_PORT, LED_IMD_OK_PIN, DRIVE_STRENGTH_HIGH);
    set_direction(LED_WARNING_PORT, LED_WARNING_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_WARNING_PORT, LED_WARNING_PIN, DRIVE_STRENGTH_HIGH);
    set_direction(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN, DRIVE_STRENGTH_HIGH);
    set_direction(LED_AMS_OK_PORT, LED_AMS_OK_PIN, GPIO_OUTPUT);
    set_drive_strength(LED_AMS_OK_PORT, LED_AMS_OK_PIN, DRIVE_STRENGTH_HIGH);

    set_direction(CONT_HV_POS_PORT, CONT_HV_POS_PIN, GPIO_OUTPUT);
    set_direction(CONT_HV_NEG_PORT, CONT_HV_NEG_PIN, GPIO_OUTPUT);
    set_direction(CONT_HV_PRE_PORT, CONT_HV_PRE_PIN, GPIO_OUTPUT);

    set_direction(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN, GPIO_INPUT);
    set_direction(IMD_STAT_PORT, IMD_STAT_PIN, GPIO_INPUT);
    set_direction(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN, GPIO_INPUT);
    set_direction(SC_STATUS_PORT, SC_STATUS_PIN, GPIO_INPUT);
    set_direction(HV_POS_STAT_PORT, HV_POS_STAT_PIN, GPIO_INPUT);
    set_direction(HV_NEG_STAT_PORT, HV_NEG_STAT_PIN, GPIO_INPUT);
    set_direction(HV_PRE_STAT_PORT, HV_PRE_STAT_PIN, GPIO_INPUT);

    set_direction(CS_RTC_PORT, CS_RTC_PIN, GPIO_OUTPUT);
    set_direction(CS_CARD_PORT, CS_CARD_PIN, GPIO_OUTPUT);
    set_direction(CS_UBATT_PORT, CS_UBATT_PIN, GPIO_OUTPUT);
    set_direction(CS_ULINK_PORT, CS_ULINK_PIN, GPIO_OUTPUT);
    set_direction(CS_SLAVES_PORT, CS_SLAVES_PIN, GPIO_OUTPUT);
    set_direction(CS_CURRENT_PORT, CS_CURRENT_PIN, GPIO_OUTPUT);
    set_direction(CS_EEPROM_PORT, CS_EEPROM_PIN, GPIO_OUTPUT);

    set_direction(INT_RTC_PORT, INT_RTC_PIN, GPIO_INPUT);
    set_direction(CARD_DETECT_PORT, CARD_DETECT_PIN, GPIO_INPUT);
    set_direction(IMD_MEAS_PORT, IMD_MEAS_PIN, GPIO_INPUT);
    set_direction(IMD_ON_DELAY_PORT, IMD_ON_DELAY_PIN, GPIO_OUTPUT);
    set_direction(AMS_FAULT_PORT, AMS_FAULT_PIN, GPIO_OUTPUT);
    set_direction(CAN_STBY_PORT, CAN_STBY_PIN, GPIO_OUTPUT);

    set_pin_mux(UART_PORT, UART_RX, 6);
    set_pin_mux(UART_PORT, UART_TX, 6);

    set_pin_mux(CAN_PORT,  CAN_RX,  5);
    set_pin_mux(CAN_PORT,  CAN_TX,  5);

    set_pin_mux(SPI0_PORT, SPI0_MOSI, 3);
    set_pin_mux(SPI0_PORT, SPI0_MISO, 3);
    set_pin_mux(SPI0_PORT, SPI0_SCK,  3);

    set_pin_mux(SPI1_PORT, SPI1_MOSI, 3);
    set_pin_mux(SPI1_PORT, SPI1_MISO, 3);
    set_pin_mux(SPI1_PORT, SPI1_SCK,  3);

    set_pin_mux(SPI2_PORT, SPI2_MOSI, 3);
    set_pin_mux(SPI2_PORT, SPI2_MISO, 3);
    set_pin_mux(SPI2_PORT, SPI2_SCK,  3);
}

int main(void)
{
	wdog_disable();
    clock_init();
    gpio_init();
    can_init(CAN0);
    uart_init();
    uart_register_receive_hook(uart_rec);
    xTaskCreate(uart_rec_task, "uart_rec", 1000, NULL, 2, &uartRecTaskHandle);

    clear_pin(CAN_STBY_PORT, CAN_STBY_PIN);

    set_pin(CS_CARD_PORT, CS_CARD_PIN);
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
    set_pin(CS_EEPROM_PORT, CS_EEPROM_PIN);
    set_pin(CS_RTC_PORT, CS_RTC_PIN);
    set_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
    set_pin(CS_UBATT_PORT, CS_UBATT_PIN);
    set_pin(CS_ULINK_PORT, CS_ULINK_PIN);

    spi_init(LPSPI1, LPSPI_PRESC_8, LPSPI_MODE_0);

    init_sensors();





    static const LTC_initial_data_t ltcInitData = {NUMBEROFSLAVES, 2700UL, 4200UL, 2};
    //ltc_init(ltcInitData);



	xTaskCreate(led_blink_task, "LED blink", 1000, NULL, 1, NULL);

	xTaskCreate(can_send_task, "CAN", 1000, NULL, 3, NULL);
	//xTaskCreate(shutdown_circuit_handle_task, "SC", 500, NULL, 3, NULL);

	vTaskStartScheduler();

    while(1); //Hopefully never reach here...

    return 0;
}

void vApplicationTickHook(void) {

}
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    PRINTF("Stack overflow in %s", pcTaskName);
	configASSERT(0);
}

void vApplicationMallocFailedHook(void) {
	configASSERT(0);
}

void vApplicationIdleHook(void) {

}

