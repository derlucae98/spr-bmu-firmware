#include "FreeRTOS.h"
#include "task.h"

#include "clock.h"
#include "gpio.h"
#include "config.h"
#include "wdt.h"
#include "uart.h"
#include "can.h"
#include "spi.h"
#include "sensors.h"

#include "contactor.h"


#include <alloca.h>
#include <string.h>

volatile uint8_t tsal = 0;

typedef struct {
    uint8_t powerCycle;
    uint8_t posAirStuck;
    uint8_t posAirBroken;
    uint8_t preStuck;
    uint8_t preBroken;
    uint8_t negAirStuck;
    uint8_t negAirBroken;
    uint8_t voltDetBroken;
} hil_data_t;

typedef struct {
    uint8_t posAirIntent;
    uint8_t posAirState;

    uint8_t preIntent;
    uint8_t preState;

    uint8_t negAirIntent;
    uint8_t negAirState;

    uint8_t voltPresent;
} uart_data_t;

volatile uart_data_t uartData;

volatile hil_data_t hilData;

volatile uint8_t uartEN;

TaskHandle_t hil_tester_taskhandle = NULL;
uint8_t requestedTest = 0;


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

void test_1(void) {
    uartEN = 1;
    vTaskDelay(pdMS_TO_TICKS(1000));
    request_tractive_system(true);
    vTaskDelay(pdMS_TO_TICKS(3300));
    request_tractive_system(false);
    vTaskDelay(pdMS_TO_TICKS(2700));
    uartEN = 0;
}

void test_2(void) {

}

void test_3(void) {

}

void test_4(void) {

}

void test_5(void) {

}

void test_6(void) {

}

void test_7(void) {

}

void test_8(void) {

}

void uart_send_task(void *p) {
    (void)p;

    TickType_t lastWake = xTaskGetTickCount();
    TickType_t period = pdMS_TO_TICKS(50);

    float voltage = 0.0f;
    while (1) {

        if (uartEN) {
            adc_data_t *adcData = get_adc_data(portMAX_DELAY);
            voltage = adcData->dcLinkVoltage;
            release_adc_data();

            //Eingänge abfragen
            uartData.posAirIntent = get_pin(AIR_POS_INTENT_PORT, AIR_POS_INTENT_PIN);
            uartData.negAirIntent = get_pin(AIR_NEG_INTENT_PORT, AIR_NEG_INTENT_PIN);
            uartData.preIntent = get_pin(AIR_PRE_INTENT_PORT, AIR_PRE_INTENT_PIN);
            uartData.posAirState = get_pin(AIR_POS_STATE_PORT, AIR_POS_STATE_PIN);
            uartData.negAirState = get_pin(AIR_NEG_STATE_PORT, AIR_NEG_STATE_PIN);
            uartData.preState = get_pin(AIR_PRE_STATE_PORT, AIR_PRE_STATE_PIN);
            uartData.voltPresent = get_pin(TSAC_HV_PORT, TSAC_HV_PIN);





            PRINTF("%.2f;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u\n", voltage, uartData.posAirIntent, uartData.posAirState, hilData.posAirStuck, hilData.posAirBroken,
                    uartData.preIntent, uartData.preState, hilData.preStuck, hilData.preBroken, uartData.negAirIntent, uartData.negAirState, hilData.negAirStuck, hilData.negAirBroken,
                    uartData.voltPresent, hilData.voltDetBroken, tsal);
        }

        vTaskDelayUntil(&lastWake, period);
    }
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
    } else if (strcmp(tokens[0], "test") == 0) {
        if (strcmp(tokens[1], "1") == 0) {
            requestedTest = 1;
            xTaskNotifyGive(hil_tester_taskhandle);
        } else if (strcmp(tokens[1], "2") == 0) {
            requestedTest = 2;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "3") == 0) {
            requestedTest = 3;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "4") == 0) {
            requestedTest = 4;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "5") == 0) {
            requestedTest = 5;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "6") == 0) {
            requestedTest = 6;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "7") == 0) {
            requestedTest = 7;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
        else if (strcmp(tokens[1], "8") == 0) {
            requestedTest = 8;
            xTaskNotifyGive(hil_tester_taskhandle);
        }
    } else if (strcmp(tokens[0], "power") == 0) {
        if (strcmp(tokens[1], "cycle") == 0) {
            hilData.powerCycle = 1;
        }
    }
}

void can_recv_task(void *p) {
    (void)p;
    can_msg_t msg;
    while (1) {
        if (xQueueReceive(can0RxQueueHandle, &msg, portMAX_DELAY)) {
            if (msg.ID == 1 && msg.DLC == 1) {
                tsal = msg.payload[0] & 0x01;
            }
        }
    }
}

void can_send_task(void *p) {
    (void)p;
    TickType_t lastWake = xTaskGetTickCount();
    TickType_t period = pdMS_TO_TICKS(50);

    can_msg_t msg;
    while (1) {
        msg.ID = 0;
        msg.DLC = 1;
        msg.payload[0] = ((hilData.powerCycle & 0x01) << 7) | ((hilData.posAirStuck & 0x01) << 6) | ((hilData.posAirBroken & 0x01) << 5)
                | ((hilData.preStuck & 0x01) << 4) | ((hilData.preBroken & 0x01) << 3) | ((hilData.negAirStuck & 0x01) << 2) | ((hilData.negAirBroken & 0x01) << 1)
                | (hilData.voltDetBroken & 0x01);
        can_send(CAN0, &msg);
        vTaskDelayUntil(&lastWake, period);
    }
}

void hil_tester_task(void *p) {
    (void)p;

    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            switch (requestedTest) {
            case 1:
                test_1();
                break;
            case 2:
                test_2();
                break;
            case 3:
                test_3();
                break;
            case 4:
                test_4();
                break;
            case 5:
                test_5();
                break;
            case 6:
                test_6();
                break;
            case 7:
                test_7();
                break;
            case 8:
                test_8();
                break;
            }
        }
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
    init_contactor();

    spi_init(LPSPI1, LPSPI_PRESC_8, LPSPI_MODE_0);


    init_adc();

    uartEN = 0;
    tsal = 0;
    memset(&hilData, 0, sizeof(hil_data_t));
    memset(&uartData, 0, sizeof(uart_data_t));

    xTaskCreate(can_recv_task, "", 1024, NULL, 2, NULL);
    xTaskCreate(can_send_task, "", 1024, NULL, 2, NULL);
    xTaskCreate(uart_send_task, "", 1024, NULL, 2, NULL);
    xTaskCreate(hil_tester_task, "", 1024, NULL, 2, &hil_tester_taskhandle);
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
}

