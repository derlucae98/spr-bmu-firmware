#include "FreeRTOS.h"
#include "task.h"
#include "S32K146.h"
#include "can.h"
#include "clock.h"
#include "gpio.h"
#include "config.h"
#include "spi.h"
#include "uart.h"
#include "bmu.h"

#include <string.h>
#include <stdarg.h>
#include <alloca.h>
#include <stdlib.h>
#include "ff.h"
#include "logger.h"
#include "rtc.h"

volatile bool sdInitPending = true;
static TaskHandle_t _sdInitTaskHandle = NULL;
static TaskHandle_t _housekeepingTaskHandle = NULL;
static FATFS FatFs;
static FIL file;

void sd_init_task(void *p) {
    (void) p;
    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            //Check if logs directory exists and if not, create it
            FILINFO fileInfo;
            FRESULT status = f_stat("logs/", &fileInfo);
            if (status == FR_NO_FILE) {
                PRINTF("Logs directory does not exist!\n");
                status = f_mkdir("logs");
                if (status != FR_OK) {
                    PRINTF("Could not create directory!\n");
                } else {
                    PRINTF("Directory created!\n");
                }
            } else {
                PRINTF("Logs directory exists!\n");
            }

            char path[32];
            PRINTF("Creating file...\n");

            uint8_t timeout = 10;
            do {
                rtc_date_time_t *dateTime = get_rtc_date_time(pdMS_TO_TICKS(1000));
                if (dateTime != NULL) {
                    snprintf(path, 32, "logs/%04u%02u%02u_%02u%02u%02u.log", dateTime->year, dateTime->month, dateTime->day, dateTime->hour, dateTime->minute, dateTime->second);
                    release_rtc_date_time();
                }
                status = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND | FA_CREATE_NEW);

                if (--timeout == 0) {
                    break;
                }
            } while (status != FR_OK);

            if (status == FR_OK) {
                logger_set_file(&file);
                PRINTF("Logger ready!\n");
                sdInitPending = false;
                logger_start();
            }
        }
    }
}

void housekeeping_task(void *p) {

    (void)p;

    TickType_t lastWakeTime;
    const TickType_t period = pdMS_TO_TICKS(50);
    lastWakeTime = xTaskGetTickCount();
    uint64_t counter = 0;

    while (1) {
        if ((counter % 2) == 0) {
            //100ms

        }

        if ((counter % 20) == 0) {
            //One second counter
            toggle_pin(LED_WARNING_PORT, LED_WARNING_PIN);
        }

        //SD insertion or removal detection
        static bool sdAvailableLast = false;
        bool sdAvailable = !get_pin(CARD_DETECT_PORT, CARD_DETECT_PIN);
        if (sdAvailable && !sdAvailableLast) {
            sd_available(true);
            PRINTF("SD inserted!\n");
            sdAvailableLast = true;
            FRESULT status = f_mount(&FatFs, "", 1);
            if (status == FR_OK) {
                PRINTF("SD ready!\n");
                if (_sdInitTaskHandle) {
                    xTaskNotifyGive(_sdInitTaskHandle);
                }
            } else {
                PRINTF("SD initialization failed!\n");
                sdInitPending = true;
            }
        } else if (!sdAvailable && sdAvailableLast) {
            sd_available(false);
            PRINTF("SD removed!\n");
            sdAvailableLast = false;
            logger_stop();
            if (!disk_status(0)) {
                f_unmount("");
            }
            sdInitPending = true;
        }

        counter++;

        vTaskDelayUntil(&lastWakeTime, period);
    }
}

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
            request_tractive_system(true);
        } else if (strcmp(tokens[1], "deactivate") == 0) {
            request_tractive_system(false);
        }
    }

    if (strcmp(tokens[0], "help") == 0) {
        PRINTF("cal <curr,ubatt,ulink> <ref,gain,offset> <val>\n");
    }

    unsigned int input[6];
    if (strcmp(tokens[0], "time") == 0) {
        if (strcmp(tokens[1], "set") == 0) {
            sscanf(tokens[2], "%u-%u-%u", &input[0], &input[1], &input[2]);
            sscanf(tokens[3], "%u:%u:%u", &input[3], &input[4], &input[5]);
            PRINTF("Setting date and time to %04u-%02u-%02u %02u:%02u:%02u\n", input[0], input[1], input[2], input[3], input[4], input[5]);
            rtc_date_time_t dateTime;
            dateTime.year = input[0];
            dateTime.month = input[1];
            dateTime.day = input[2];
            dateTime.hour = input[3];
            dateTime.minute = input[4];
            dateTime.second = input[5];
            rtc_set_date_time(&dateTime);
        }
    }
    write_calibration(calibration);
    reload_calibration();

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
//    set_direction(LED_AMS_OK_PORT, LED_AMS_OK_PIN, GPIO_OUTPUT);
//    set_drive_strength(LED_AMS_OK_PORT, LED_AMS_OK_PIN, DRIVE_STRENGTH_HIGH);

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

    set_direction(TP_1_PORT, TP_1_PIN, GPIO_OUTPUT);
    set_direction(TP_2_PORT, TP_2_PIN, GPIO_OUTPUT);
    set_direction(TP_3_PORT, TP_3_PIN, GPIO_OUTPUT);
    set_direction(TP_4_PORT, TP_4_PIN, GPIO_OUTPUT);
    set_direction(TP_5_PORT, TP_5_PIN, GPIO_OUTPUT);
    set_direction(TP_6_PORT, TP_6_PIN, GPIO_OUTPUT);
    set_direction(TP_7_PORT, TP_7_PIN, GPIO_OUTPUT);
    set_direction(TP_8_PORT, TP_8_PIN, GPIO_OUTPUT);


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

void tick_hook(void) {
    char* timestamp = NULL;
    timestamp = rtc_get_timestamp(pdMS_TO_TICKS(1000));
    if (timestamp != NULL) {
//        PRINTF("%s\n", timestamp);
        logger_tick_hook();
    }
}

void init_task(void *p) {
    (void) p;
    while (1) {
        rtc_register_tick_hook(tick_hook);
        init_rtc();

        uint32_t resetReason = RCM->SRS;
        PRINTF("Reset reason: 0x%X\n", resetReason);
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


        xTaskCreate(uart_rec_task, "uart_rec", 1000, NULL, 2, &uartRecTaskHandle);
        init_bmu();
        logger_init();
        xTaskCreate(sd_init_task, "sd init", 400, NULL, 2, &_sdInitTaskHandle);
        xTaskCreate(housekeeping_task, "housekeeping", 300, NULL, 3, &_housekeepingTaskHandle);
        vTaskDelete(NULL);
    }
}

int main(void)
{
	wdog_disable();
    clock_init();
    gpio_init();
    can_init(CAN0);
    uart_init(false);
    uart_register_receive_hook(uart_rec);

    clear_pin(CAN_STBY_PORT, CAN_STBY_PIN);

    set_pin(CS_CARD_PORT, CS_CARD_PIN);
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
    set_pin(CS_EEPROM_PORT, CS_EEPROM_PIN);
    clear_pin(CS_RTC_PORT, CS_RTC_PIN); //RTC chip enable is active high
    set_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
    set_pin(CS_UBATT_PORT, CS_UBATT_PIN);
    set_pin(CS_ULINK_PORT, CS_ULINK_PIN);

    spi_init(LPSPI0, LPSPI_PRESC_2, LPSPI_MODE_0);
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
    (void) pxTask;
    (void) pcTaskName;
	configASSERT(0);
}

void vApplicationMallocFailedHook(void) {
	configASSERT(0);
}

void vApplicationIdleHook(void) {
    toggle_pin(TP_3_PORT, TP_3_PIN);
}

