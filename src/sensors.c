/*
 * sensors.c
 *
 *  Created on: Feb 4, 2022
 *      Author: scuderia
 */

#include "sensors.h"

#define SHUNT 16.4f
#define CONVERSION_RATIO 2000
#define CURRENT_OFFSET -0.02258f

static mcp356x_obj_t currentSensor;

extern void PRINTF(const char *format, ...);
static void sensor_task(void *p);

static inline void current_sensor_spi(uint8_t *a, size_t len) {
    spi_move_array(LPSPI1, a, len);
}

static inline void current_sensor_assert(void) {
    clear_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

static inline void current_sensor_deassert(void) {
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

bool init_sensors() {
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

    xTaskCreate(sensor_task, "sensors", 1000, NULL, 4, NULL);
    return true;
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
        mcp356x_read_voltage(&currentSensor, 2.498f, &val);

        current = (((2.498f / 2) - val - (CURRENT_OFFSET)) / SHUNT) * CONVERSION_RATIO;
        PRINTF("I: %.2f A\n", current);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

