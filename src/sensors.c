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

static SemaphoreHandle_t _sensorDataMutex = NULL;
static sensor_data_t _sensorData;

extern void PRINTF(const char *format, ...);
static void sensor_task(void *p);
static sensor_calibration_t default_calibration(void);

static BaseType_t sensor_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_sensorDataMutex, blocktime);
}

void release_sensor_data(void) {
    xSemaphoreGive(_sensorDataMutex);
}

sensor_data_t* get_sensor_data(TickType_t blocktime) {
    if (sensor_mutex_take(blocktime)) {
        return &_sensorData;
    } else {
        return NULL;
    }
}

bool copy_sensor_data(sensor_data_t *dest, TickType_t blocktime) {
    sensor_data_t *src = get_sensor_data(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(sensor_data_t));
        release_sensor_data();
        return true;
    } else {
        return false;
    }
}

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
    _sensorDataMutex = xSemaphoreCreateMutex();
    configASSERT(_sensorDataMutex);
    memset(&_sensorData, 0, sizeof(sensor_data_t));

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
    xTaskCreate(sensor_task, "sensors", 1000, NULL, 4, NULL);
    return true;
}

sensor_calibration_t load_calibration(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];

    bool uncal = true;
    sensor_calibration_t cal;

    if (eeprom_mutex_take(pdMS_TO_TICKS(500))) {
        eeprom_read_array(pageBuffer, 0, EEPROM_PAGESIZE);
        uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];


        for (size_t i = 0; i < EEPROM_PAGESIZE; i++) {
            if (pageBuffer[i] != 0xFF) {
                uncal = false;
            }
        }

        if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
            PRINTF("CRC error while loading calibration data!\n");
            configASSERT(0);
        }

        eeprom_mutex_give();
    }

    if (uncal) {
        PRINTF("No calibration data found!\n");
        return default_calibration();
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

    if (eeprom_mutex_take(pdMS_TO_TICKS(500))) {
        eeprom_write_page(pageBuffer, 0x0, sizeof(pageBuffer));
        uint8_t timeout = 200; //2 seconds
        while(!eeprom_has_write_finished()) {
            if (--timeout > 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            } else {
                PRINTF("Writing calibration data to EEPROM timed out!\n");
                eeprom_mutex_give();
                return false;
            }
        }
        eeprom_mutex_give();
        return true;
    }
    return false;
}

static void sensor_task(void *p) {
    (void) p;

    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(25);
    xLastWakeTime = xTaskGetTickCount();
    float current = 0.0f;
    float ubatVolt = 0.0f;
    float ulinkVolt = 0.0f;
    bool currentSensorError = false;
    bool batteryVoltageError = false;
    bool dcLinkVoltageError = false;
    while (1) {

        // If an error occurred, try to reset the sensor to clear the error
        if (currentSensorError) {
            mcp356x_reset(&_currentSensor);
            mcp356x_set_config(&_currentSensor);
        }
        if (batteryVoltageError) {
            mcp356x_reset(&_ubat);
            mcp356x_set_config(&_ubat);
        }
        if (dcLinkVoltageError) {
            mcp356x_reset(&_ulink);
            mcp356x_set_config(&_ulink);
        }


        if (mcp356x_acquire(&_currentSensor, MUX_CH0, MUX_CH3) != MCP356X_ERROR_OK) {
            currentSensorError = true;
        }
        if (mcp356x_acquire(&_ubat, MUX_CH0, MUX_AGND) != MCP356X_ERROR_OK) {
            batteryVoltageError = true;
        }
        if (mcp356x_acquire(&_ulink, MUX_CH0, MUX_AGND) != MCP356X_ERROR_OK) {
            dcLinkVoltageError = true;
        }

        vTaskDelay(pdMS_TO_TICKS(6));

        if (mcp356x_read_voltage(&_currentSensor, _cal.current_1_ref, &current) == MCP356X_ERROR_OK) {
            currentSensorError = false;
        } else {
            currentSensorError = true;
        }

        if (mcp356x_read_voltage(&_ubat, _cal.ubatt_ref, &ubatVolt) == MCP356X_ERROR_OK) {
            batteryVoltageError = false;
        } else {
            batteryVoltageError = true;
        }

        if (mcp356x_read_voltage(&_ulink, _cal.ulink_ref, &ulinkVolt) == MCP356X_ERROR_OK) {
            dcLinkVoltageError = false;
        } else {
            dcLinkVoltageError = true;
        }

        current = (current / SHUNT) * CURRENT_CONVERSION_RATIO;
        current = (current + _cal.current_1_offset) * _cal.current_1_gain;
        current = current * (-1); //Reverse current direction

        ubatVolt = ubatVolt * VOLTAGE_CONVERSION_RATIO;
        ubatVolt = (ubatVolt + _cal.ubatt_offset) * _cal.ubatt_gain;

        ulinkVolt = ulinkVolt * VOLTAGE_CONVERSION_RATIO;
        ulinkVolt = (ulinkVolt + _cal.ulink_offset) * _cal.ulink_gain;

//        PRINTF("Ubat: %.3f V\n", ubatVolt);
//        PRINTF("Ulink: %.3f V\n", ulinkVolt);

        if (!currentSensorError) {
//            PRINTF("I: %.2f A\n", current);
        } else {
            PRINTF("Current sensor failed!\n");
        }


        sensor_data_t *sensorData = get_sensor_data(pdMS_TO_TICKS(4));
        if (sensorData != NULL) {
            sensorData->current = current;
            sensorData->batteryVoltage = ubatVolt;
            sensorData->dcLinkVoltage = ulinkVolt;
            sensorData->currentValid = !currentSensorError;
            sensorData->batteryVoltageValid = !batteryVoltageError;
            sensorData->dcLinkVoltageValid = !dcLinkVoltageError;
            release_sensor_data();
        } else {
            PRINTF("Sensor: Can't get mutex!\n");
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

sensor_calibration_t default_calibration(void) {
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

void print_calibration(void) {
    sensor_calibration_t calData;
    calData = load_calibration();
    PRINTF("Calibration data:\n");
    PRINTF("Current ref:    %.6f\n", calData.current_1_ref);
    PRINTF("Current gain:   %.6f\n", calData.current_1_gain);
    PRINTF("Current offset: %.6f\n", calData.current_1_offset);
    PRINTF("ULINK ref:      %.6f\n", calData.ulink_ref);
    PRINTF("ULINK gain:     %.6f\n", calData.ulink_gain);
    PRINTF("ULINK offset:   %.6f\n", calData.ulink_offset);
    PRINTF("UBATT ref:      %.6f\n", calData.ubatt_ref);
    PRINTF("UBATT gain:     %.6f\n", calData.ubatt_gain);
    PRINTF("UBATT offset:   %.6f\n", calData.ubatt_offset);
}
