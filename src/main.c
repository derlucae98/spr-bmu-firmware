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
#include "eeprom.h"
#include "bmu.h"

#include <string.h>
#include <stdarg.h>
#include <alloca.h>
#include <stdlib.h>




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
    float calVal;

    sensor_calibration_t calibration = load_calibration();

    if (strcmp(tokens[0], "cal") == 0) {
        if (strcmp(tokens[1], "curr") == 0) {
            if (strcmp(tokens[2], "gain") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating gain for %s: %.3f\n", tokens[1], calVal);
                calibration.current_1_gain = calVal;
            } else if (strcmp(tokens[2], "offset") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating offset for %s: %.3f\n", tokens[1], calVal);
                calibration.current_1_offset = calVal;
            } else if (strcmp(tokens[2], "ref") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating reference for %s: %.3f\n", tokens[1], calVal);
                calibration.current_1_ref = calVal;
            }
        }
        if (strcmp(tokens[1], "ubatt") == 0) {
            if (strcmp(tokens[2], "gain") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating gain for %s: %.3f\n", tokens[1], calVal);
                calibration.ubatt_gain = calVal;
            } else if (strcmp(tokens[2], "offset") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating offset for %s: %.3f\n", tokens[1], calVal);
                calibration.ubatt_offset = calVal;
            } else if (strcmp(tokens[2], "ref") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating reference for %s: %.3f\n", tokens[1], calVal);
                calibration.ubatt_ref = calVal;
            }
        }
        if (strcmp(tokens[1], "ulink") == 0) {
            if (strcmp(tokens[2], "gain") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating gain for %s: %.3f\n", tokens[1], calVal);
                calibration.ulink_gain = calVal;
            } else if (strcmp(tokens[2], "offset") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating offset for %s: %.3f\n", tokens[1], calVal);
                calibration.ulink_offset = calVal;
            } else if (strcmp(tokens[2], "ref") == 0) {
                sscanf(tokens[3], "%f", &calVal);
                PRINTF("calibrating reference for %s: %.3f\n", tokens[1], calVal);
                calibration.ulink_ref = calVal;
            }
        }
    }

    if (strcmp(tokens[0], "ts") == 0) {
        if (strcmp(tokens[1], "activate") == 0) {
            contactorEvent = EVENT_TS_ACTIVATE;
        } else if (strcmp(tokens[1], "deactivate") == 0) {
            contactorEvent = EVENT_TS_DEACTIVATE;
        }
    } else if (strcmp(tokens[0], "pre") == 0) {
        if (strcmp(tokens[1], "success") == 0) {
            contactorEvent = EVENT_PRE_CHARGE_SUCCESSFUL;
        } else if (strcmp(tokens[1], "fail") == 0) {
            contactorEvent = EVENT_ERROR;
        }
    } else if (strcmp(tokens[0], "error") == 0) {
        contactorEvent = EVENT_ERROR;
    } else if (strcmp(tokens[0], "clear") == 0) {
        contactorEvent = EVENT_ERROR_CLEARED;
    }

    write_calibration(calibration);
    reload_calibration();

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

void init_task(void *p) {
    (void) p;

    while (1) {
        xTaskCreate(uart_rec_task, "uart_rec", 1000, NULL, 2, &uartRecTaskHandle);
        init_bmu();
        xTaskCreate(led_blink_task, "LED blink", 1000, NULL, 1, NULL);

        vTaskDelete(NULL);
    }

}


int main(void)
{
	wdog_disable();
    clock_init();
    gpio_init();
    can_init(CAN0);
    uart_init();
    uart_register_receive_hook(uart_rec);

    clear_pin(CAN_STBY_PORT, CAN_STBY_PIN);

    set_pin(CS_CARD_PORT, CS_CARD_PIN);
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
    set_pin(CS_EEPROM_PORT, CS_EEPROM_PIN);
    set_pin(CS_RTC_PORT, CS_RTC_PIN);
    set_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
    set_pin(CS_UBATT_PORT, CS_UBATT_PIN);
    set_pin(CS_ULINK_PORT, CS_ULINK_PIN);

    spi_init(LPSPI0, LPSPI_PRESC_8, LPSPI_MODE_0);
    spi_init(LPSPI1, LPSPI_PRESC_8, LPSPI_MODE_0);
    spi_init(LPSPI2, LPSPI_PRESC_8, LPSPI_MODE_3);

    xTaskCreate(init_task, "init", 1000, NULL, configMAX_PRIORITIES-1, NULL);

	vTaskStartScheduler();

    while(1); //Hopefully never reach here...

    return 0;
}

void vApplicationTickHook(void) {

}
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
	configASSERT(0);
}

void vApplicationMallocFailedHook(void) {
	configASSERT(0);
}

void vApplicationIdleHook(void) {

}

