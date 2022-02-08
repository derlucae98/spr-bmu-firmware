/*
 * sensors.c
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#include "sensors.h"

#define SHUNT 16.4f
#define CURRENT_CONVERSION_RATIO 2000
#define VOLTAGE_CONVERSION_RATIO 251.0f

static mcp356x_obj_t _currentSensor;
static mcp356x_obj_t _ubat;
static sensor_calibration_t _cal;

extern void PRINTF(const char *format, ...);
static void sensor_task(void *p);

static inline void sensor_spi(uint8_t *a, size_t len) {
    spi_move_array(SENSOR_SPI, a, len);
}

static inline void current_sensor_assert(void) {
    clear_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

static inline void current_sensor_deassert(void) {
    set_pin(CS_CURRENT_PORT, CS_CURRENT_PIN);
}

static inline void ubat_assert(void) {
    clear_pin(CS_UBATT_PORT, CS_UBATT_PIN);
}

static inline void ubat_deassert(void) {
    set_pin(CS_UBATT_PORT, CS_UBATT_PIN);
}

bool init_sensors(void) {
    _currentSensor = mcp356x_init(sensor_spi, current_sensor_assert, current_sensor_deassert);
    _ubat = mcp356x_init(sensor_spi, ubat_assert, ubat_deassert);

    _currentSensor.config.VREF_SEL = VREF_SEL_EXT;
    _currentSensor.config.CLK_SEL = CLK_SEL_INT;
    _currentSensor.config.ADC_MODE = ADC_MODE_CONV;
    _currentSensor.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    _currentSensor.config.DATA_FORMAT = DATA_FORMAT_32_SGN;
    _currentSensor.config.CRC_FORMAT = CRC_FORMAT_16;
    _currentSensor.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    _currentSensor.config.EN_STP = 0;
    _currentSensor.config.OSR = OSR_4096;
    _currentSensor.config.EN_CRCCOM = 1;

    mcp356x_error_t err = MCP356X_ERROR_FAILED;
//    err = mcp356x_reset(&currentSensor);
//
//    if (err != MCP356X_ERROR_OK) {
//        return false;
//    }
//
//    err = mcp356x_set_config(&currentSensor);
//    if (err != MCP356X_ERROR_OK) {
//        return false;
//    }

    _ubat.config.VREF_SEL = VREF_SEL_EXT;
    _ubat.config.CLK_SEL = CLK_SEL_INT;
    _ubat.config.ADC_MODE = ADC_MODE_CONV;
    _ubat.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    _ubat.config.DATA_FORMAT = DATA_FORMAT_32_SGN;
    _ubat.config.CRC_FORMAT = CRC_FORMAT_16;
    _ubat.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    _ubat.config.EN_STP = 0;
    _ubat.config.OSR = OSR_4096;
    _ubat.config.EN_CRCCOM = 1;

    err = mcp356x_reset(&_ubat);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("UBATT init failed!\n");
        return false;
    }

    err = mcp356x_set_config(&_ubat);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("UBATT init failed!\n");
        return false;
    }

    _cal = load_calibration();
    xTaskCreate(sensor_task, "sensors", 1000, NULL, 2, NULL);
    return true;
}

sensor_calibration_t load_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    eeprom_read_array(pageBuffer, 0, EEPROM_PAGESIZE);
    uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];
    sensor_calibration_t cal;
    if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading calibration data!\n");
        configASSERT(0);
    }
    memcpy(&cal, &pageBuffer[0], sizeof(sensor_calibration_t));
    return cal;
}

bool write_calibration(sensor_calibration_t cal) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0xFF, sizeof(pageBuffer));
    memcpy(&pageBuffer[0], &cal, sizeof(sensor_calibration_t));
    uint16_t crc = eeprom_get_crc16(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t));
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;
    eeprom_write_page(pageBuffer, 0x0, sizeof(pageBuffer));
    uint8_t timeout = 200; //2 seconds
    while(!eeprom_has_write_finished()) {
        if (--timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            PRINTF("Writing calibration data to EEPROM timed out!\n");
            return false;
        }
    }
    return true;
}

static void sensor_task(void *p) {
    (void) p;

    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();
    float current = 0;
    float ubatVolt = 0;
    while (1) {

        mcp356x_acquire(&_currentSensor, MUX_CH0, MUX_CH3);
        mcp356x_acquire(&_ubat, MUX_CH0, MUX_AGND);
        vTaskDelay(pdMS_TO_TICKS(6));
        mcp356x_read_voltage(&_currentSensor, _cal.current_1_ref, &current);
        mcp356x_read_voltage(&_ubat, _cal.voltage_1_ref, &ubatVolt);

        current = (current / SHUNT) * CURRENT_CONVERSION_RATIO;
        current = (current + _cal.current_1_offset) * _cal.current_1_gain;

        ubatVolt = ubatVolt * VOLTAGE_CONVERSION_RATIO;
        ubatVolt = (ubatVolt + _cal.voltage_1_offset) * _cal.voltage_1_gain;

        PRINTF("Ubat: %.3f\n", ubatVolt);
        PRINTF("I: %.2f A\n", current);


        if (batteryData_mutex_take(pdMS_TO_TICKS(4))) {
            batteryData.current = (int16_t) (current * 100);
            batteryData_mutex_give();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

