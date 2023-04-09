
#include "FreeRTOS.h"
#include "task.h"

#include "clock.h"
#include "gpio.h"
#include "config.h"
#include "wdt.h"
#include "uart.h"
#include "can.h"
#include "spi.h"
#include "stacks.h"
#include "bmu.h"
#include "adc.h"
#include "contactor.h"


#include <alloca.h>
#include <string.h>


static void set_gpio_config(void) {
    gpio_enable_clock(PORTA);
    gpio_enable_clock(PORTB);
    gpio_enable_clock(PORTC);
    gpio_enable_clock(PORTD);
    gpio_enable_clock(PORTE);
    GPIO_OUT_STRONG(LED_IMD_FAULT_PORT, LED_IMD_FAULT_PIN, 0);
    GPIO_OUT_STRONG(LED_IMD_OK_PORT, LED_IMD_OK_PIN, 0);
    GPIO_OUT_STRONG(LED_AMS_FAULT_PORT, LED_AMS_FAULT_PIN, 0);
    GPIO_OUT_STRONG(LED_AMS_OK_PORT, LED_AMS_OK_PIN, 0);
    GPIO_OUT_STRONG(LED_WARNING_PORT, LED_WARNING_PIN, 0);
    GPIO_OUT_STRONG(LED_CARD_PORT, LED_CARD_PIN, 0);
    GPIO_OUT_STRONG(AIR_POS_SET_PORT, AIR_POS_SET_PIN, 0);
    GPIO_OUT_STRONG(AIR_POS_CLR_PORT, AIR_POS_CLR_PIN, 0);
    GPIO_OUT_STRONG(AIR_NEG_SET_PORT, AIR_NEG_SET_PIN, 0);
    GPIO_OUT_STRONG(AIR_NEG_CLR_PORT, AIR_NEG_CLR_PIN, 0);
    GPIO_OUT_STRONG(AIR_PRE_SET_PORT, AIR_PRE_SET_PIN, 0);
    GPIO_OUT_STRONG(AIR_PRE_CLR_PORT, AIR_PRE_CLR_PIN, 0);
    GPIO_IN(CARD_DETECT_PORT, CARD_DETECT_PIN, PULL_NONE);
    GPIO_OUT_STRONG(CS_CARD_PORT, CS_CARD_PIN, 1);
    GPIO_OUT_STRONG(CS_RTC_PORT, CS_RTC_PIN, 0);
    GPIO_IN(CLKOUT_RTC_PORT, CLKOUT_RTC_PIN, PULL_NONE);
    GPIO_IN(IRQ_RTC_PORT, IRQ_RTC_PIN, PULL_NONE);
    GPIO_OUT_STRONG(CS_EEPROM_PORT, CS_EEPROM_PIN, 1);
    GPIO_OUT_STRONG(CS_ADC_PORT, CS_ADC_PIN, 1);
    GPIO_IN(IRQ_ADC_PORT, IRQ_ADC_PIN, PULL_NONE);
    GPIO_OUT_STRONG(CS_SLAVES_PORT, CS_SLAVES_PIN, 1);
    GPIO_IN(AMS_RES_STAT_PORT, AMS_RES_STAT_PIN, PULL_NONE);
    GPIO_IN(IMD_RES_STAT_PORT, IMD_RES_STAT_PIN, PULL_NONE);
    GPIO_IN(IMD_STAT_PORT, IMD_STAT_PIN, PULL_NONE);
    GPIO_IN(SC_STATUS_PORT, SC_STATUS_PIN, PULL_NONE);
    GPIO_IN(IMD_MEAS_PORT, IMD_MEAS_PIN, PULL_NONE);
    GPIO_OUT_STRONG(AMS_FAULT_PORT, AMS_FAULT_PIN, 0);
    GPIO_IN(TSAC_HV_PORT, TSAC_HV_PIN, PULL_NONE);

    GPIO_IN(AIR_POS_INTENT_PORT, AIR_POS_INTENT_PIN, PULL_NONE);
    GPIO_IN(AIR_NEG_INTENT_PORT, AIR_NEG_INTENT_PIN, PULL_NONE);
    GPIO_IN(AIR_PRE_INTENT_PORT, AIR_PRE_INTENT_PIN, PULL_NONE);
    GPIO_IN(AIR_POS_STATE_PORT, AIR_POS_STATE_PIN, PULL_NONE);
    GPIO_IN(AIR_NEG_STATE_PORT, AIR_NEG_STATE_PIN, PULL_NONE);
    GPIO_IN(AIR_PRE_STATE_PORT, AIR_PRE_STATE_PIN, PULL_NONE);

    GPIO_OUT_STRONG(DBG_1_PORT, DBG_1_PIN, 0);
    GPIO_OUT_STRONG(DBG_2_PORT, DBG_2_PIN, 0);
    GPIO_OUT_STRONG(DBG_3_PORT, DBG_3_PIN, 0);
    GPIO_OUT_STRONG(DBG_4_PORT, DBG_4_PIN, 0);
    GPIO_OUT_STRONG(DBG_5_PORT, DBG_5_PIN, 0);
    GPIO_OUT_STRONG(DBG_6_PORT, DBG_6_PIN, 0);
    GPIO_OUT_STRONG(DBG_7_PORT, DBG_7_PIN, 0);
    GPIO_OUT_STRONG(DBG_8_PORT, DBG_8_PIN, 0);
    GPIO_OUT_STRONG(DBG_9_PORT, DBG_9_PIN, 0);

    set_pin_mux(SPI0_SCK_PORT, SPI0_SCK_PIN, 4);
    set_pin_mux(SPI0_MISO_PORT, SPI0_MISO_PIN, 4);
    set_pin_mux(SPI0_MOSI_PORT, SPI0_MOSI_PIN, 4);
    set_pin_mux(SPI1_SCK_PORT, SPI1_SCK_PIN, 3);
    set_pin_mux(SPI1_MISO_PORT, SPI1_MISO_PIN, 3);
    set_pin_mux(SPI1_MOSI_PORT, SPI1_MOSI_PIN, 3);
    set_pin_mux(SPI2_SCK_PORT, SPI2_SCK_PIN, 3);
    set_pin_mux(SPI2_MISO_PORT, SPI2_MISO_PIN, 3);
    set_pin_mux(SPI2_MOSI_PORT, SPI2_MOSI_PIN, 3);
    set_pin_mux(CAN_DIAG_PORT, CAN_DIAG_RX, 3);
    set_pin_mux(CAN_DIAG_PORT, CAN_DIAG_TX, 3);
    set_pin_mux(CAN_VEHIC_PORT, CAN_VEHIC_RX, 5);
    set_pin_mux(CAN_VEHIC_PORT, CAN_VEHIC_TX, 5);
    set_pin_mux(UART_PORT, UART_RX, 4);
    set_pin_mux(UART_PORT, UART_TX, 4);
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

    if (strcmp(tokens[0], "ts") == 0) {
        if (strcmp(tokens[1], "on") == 0) {
            request_tractive_system(true);
        } else if (strcmp(tokens[1], "off") == 0) {
            request_tractive_system(false);
        }
    }
}


void init_task(void *p) {
    (void)p;

    while (1) {
        init_adc();
        init_contactor();
        init_bmu();
        init_stacks();

        vTaskDelete(NULL);
    }
}

int main(void)
{
    disable_wdt();
    clock_init();
    set_gpio_config();
    can_init(CAN0);
    can_init(CAN1);
    uart_init(true);
    uart_register_receive_hook(uart_rec);
    set_pin(AMS_FAULT_PORT, AMS_FAULT_PIN);



    spi_init(LPSPI1, LPSPI_PRESC_1, LPSPI_MODE_0);
    spi_init(LPSPI0, LPSPI_PRESC_4, LPSPI_MODE_3);
//    spi_enable_dma(LPSPI1);




    xTaskCreate(init_task, "", 1000, NULL, configMAX_PRIORITIES-1, NULL);

    vTaskStartScheduler();

    while(1); //Hopefully never reach here...

    return 0;
}

void vApplicationTickHook(void) {

}
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    (void) pxTask;
    (void) pcTaskName;
    configASSERT(0);
}

void vApplicationMallocFailedHook(void) {
    configASSERT(0);
}

void vApplicationIdleHook(void) {
    toggle_pin(DBG_9_PORT, DBG_9_PIN);
}

