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
static mcp356x_obj_t _ulink;
static volatile sensor_calibration_t _cal;

extern void PRINTF(const char *format, ...);
static void _sensor_task(void *p);
static sensor_calibration_t _default_calibration(void);

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

static inline void ulink_assert(void) {
    clear_pin(CS_ULINK_PORT, CS_ULINK_PIN);
}

static inline void ulink_deassert(void) {
    set_pin(CS_ULINK_PORT, CS_ULINK_PIN);
}

bool init_sensors(void) {
    mcp356x_error_t err;

    _currentSensor = mcp356x_init(sensor_spi, current_sensor_assert, current_sensor_deassert);
    _ubat = mcp356x_init(sensor_spi, ubat_assert, ubat_deassert);
    _ulink = mcp356x_init(sensor_spi, ulink_assert, ulink_deassert);

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

    _ulink.config.VREF_SEL = VREF_SEL_EXT;
    _ulink.config.CLK_SEL = CLK_SEL_INT;
    _ulink.config.ADC_MODE = ADC_MODE_CONV;
    _ulink.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    _ulink.config.DATA_FORMAT = DATA_FORMAT_32_SGN;
    _ulink.config.CRC_FORMAT = CRC_FORMAT_16;
    _ulink.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    _ulink.config.EN_STP = 0;
    _ulink.config.OSR = OSR_4096;
    _ulink.config.EN_CRCCOM = 1;

    err = mcp356x_reset(&_currentSensor);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("Current sensor init failed!\n");
        return false;
    }

    err = mcp356x_set_config(&_currentSensor);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("Current sensor init failed!\n");
        return false;
    }

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

    err = mcp356x_reset(&_ulink);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("ULINK init failed!\n");
        return false;
    }

    err = mcp356x_set_config(&_ulink);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("ULINK init failed!\n");
        return false;
    }

    reload_calibration();
    xTaskCreate(_sensor_task, "sensors", 1000, NULL, 2, NULL);
    return true;
}

sensor_calibration_t load_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    eeprom_read_array(pageBuffer, 0, EEPROM_PAGESIZE);
    uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];
    sensor_calibration_t cal;
    bool uncal = true;

    for (size_t i = 0; i < EEPROM_PAGESIZE; i++) {
        if (pageBuffer[i] != 0xFF) {
            uncal = false;
        }
    }

    if (uncal) {
        PRINTF("No calibration data found!\n");
        return _default_calibration();
    }

    if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading calibration data!\n");
        configASSERT(0);
    }

    memcpy(&cal, &pageBuffer[0], sizeof(sensor_calibration_t));
    return cal;
}

void reload_calibration(void) {
    _cal = load_calibration();
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

static void _sensor_task(void *p) {
    (void) p;

    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    xLastWakeTime = xTaskGetTickCount();
    float current = 0.0f;
    float ubatVolt = 0.0f;
    float ulinkVolt = 0.0f;
    while (1) {

        mcp356x_acquire(&_currentSensor, MUX_CH0, MUX_CH3);
        mcp356x_acquire(&_ubat, MUX_CH0, MUX_AGND);
        mcp356x_acquire(&_ulink, MUX_CH0, MUX_AGND);
        vTaskDelay(pdMS_TO_TICKS(6));
        mcp356x_read_voltage(&_currentSensor, _cal.current_1_ref, &current);
        mcp356x_read_voltage(&_ubat, _cal.ubatt_ref, &ubatVolt);
        mcp356x_read_voltage(&_ulink, _cal.ulink_ref, &ulinkVolt);

        current = (current / SHUNT) * CURRENT_CONVERSION_RATIO;
        current = (current + _cal.current_1_offset) * _cal.current_1_gain;

        ubatVolt = ubatVolt * VOLTAGE_CONVERSION_RATIO;
        ubatVolt = (ubatVolt + _cal.ubatt_offset) * _cal.ubatt_gain;

        ulinkVolt = ulinkVolt * VOLTAGE_CONVERSION_RATIO;
        ulinkVolt = (ulinkVolt + _cal.ulink_offset) * _cal.ulink_gain;

        PRINTF("Ubat: %.3f V\n", ubatVolt);
        PRINTF("Ulink: %.3f V\n", ulinkVolt);
        PRINTF("I: %.2f A\n", current);

        if (batteryData_mutex_take(pdMS_TO_TICKS(4))) {
            batteryData.current = current;
            batteryData.batteryVoltage = ubatVolt;
            batteryData.dcLinkVoltage = ulinkVolt;
            batteryData_mutex_give();
        } else {
            PRINTF("Sensor: Can't get mutex!\n");
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

sensor_calibration_t _default_calibration(void) {
    sensor_calibration_t cal;
    cal.current_1_ref = 2.5f;
    cal.current_1_gain = 1.0f;
    cal.current_1_offset = 0.0f;
    cal.current_2_ref = 2.5f;
    cal.current_2_gain = 1.0f;
    cal.current_2_offset = 0.0f;
    cal.ubatt_ref = 2.5f;
    cal.ubatt_gain = 1.0f;
    cal.ubatt_offset = 0.0f;
    cal.ulink_ref = 2.5f;
    cal.ulink_gain = 1.0f;
    cal.ulink_offset = 0.0f;
    return cal;
}
