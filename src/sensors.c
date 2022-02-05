/*
 * sensors.c
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#include "sensors.h"

#define SHUNT 16.4f
#define CONVERSION_RATIO 2000

static mcp356x_obj_t currentSensor;

extern void PRINTF(const char *format, ...);
static void sensor_task(void *p);
volatile float currGain;
volatile float currOffset;
volatile float currRef;
volatile float currFEGain; //Front-end gain
volatile float currFEOffset; //Front-end offset

static inline void current_sensor_spi(uint8_t *a, size_t len) {
    spi_move_array(SENSOR_SPI, a, len);
}

static inline void current_sensor_assert(void) {
    clear_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

static inline void current_sensor_deassert(void) {
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

bool init_sensors(void) {
    currentSensor = mcp356x_init(current_sensor_spi, current_sensor_assert, current_sensor_deassert);

    currentSensor.config.VREF_SEL = VREF_SEL_EXT;
    currentSensor.config.CLK_SEL = CLK_SEL_INT;
    currentSensor.config.ADC_MODE = ADC_MODE_CONV;
    currentSensor.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    currentSensor.config.DATA_FORMAT = DATA_FORMAT_32_SGN;
    currentSensor.config.CRC_FORMAT = CRC_FORMAT_16;
    currentSensor.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    currentSensor.config.EN_STP = 0;
    currentSensor.config.OSR = OSR_4096;
    currentSensor.config.EN_CRCCOM = 1;
    mcp356x_error_t err = MCP356X_ERROR_FAILED;
    err = mcp356x_reset(&currentSensor);

    if (err != MCP356X_ERROR_OK) {
        return false;
    }

    err = mcp356x_set_config(&currentSensor);
    if (err != MCP356X_ERROR_OK) {
        return false;
    }

    load_calibration();
    xTaskCreate(sensor_task, "sensors", 1000, NULL, 2, NULL);
    return true;
}

void load_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    eeprom_read_array(pageBuffer, 0, EEPROM_PAGESIZE);
    uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];
    if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading calibration data!\n");
        configASSERT(0);
    }

    memcpy((float*)&currGain,     &pageBuffer[0],  sizeof(float));
    memcpy((float*)&currOffset,   &pageBuffer[4],  sizeof(float));
    memcpy((float*)&currRef,      &pageBuffer[8],  sizeof(float));
    memcpy((float*)&currFEGain,   &pageBuffer[12], sizeof(float));
    memcpy((float*)&currFEOffset, &pageBuffer[16], sizeof(float));

}

static void sensor_task(void *p) {
    (void) p;

    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();
    float val;
    float current = 0;
    while (1) {

        mcp356x_acquire(&currentSensor, MUX_CH0, MUX_CH3);
        vTaskDelay(pdMS_TO_TICKS(6));
        mcp356x_read_voltage(&currentSensor, currRef, &val);

        val = (val + currOffset) * currGain;
        //PRINTF("U pre: %0.4f V\n", val);

        val = (val - currFEOffset) / (currFEGain);
        //PRINTF("U post: %0.4f V\n", val);
        current = (val / SHUNT) * CONVERSION_RATIO;
        //PRINTF("I: %.2f A\n", current);
        if (batteryData_mutex_take(pdMS_TO_TICKS(4))) {
            batteryData.current = (int16_t) (current * 100);
            batteryData_mutex_give();
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

