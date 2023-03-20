/*
 * adcs.c
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#include "sensors.h"

static mcp356x_obj_t _adc;
static volatile adc_calibration_t _cal;

static SemaphoreHandle_t _adcDataMutex = NULL;
static adc_data_t _adcData;

extern void PRINTF(const char *format, ...);
static void adc_task(void *p);


static BaseType_t adc_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_adcDataMutex, blocktime);
}

void release_adc_data(void) {
    xSemaphoreGive(_adcDataMutex);
}

adc_data_t* get_adc_data(TickType_t blocktime) {
    if (adc_mutex_take(blocktime)) {
        return &_adcData;
    } else {
        return NULL;
    }
}

bool copy_adc_data(adc_data_t *dest, TickType_t blocktime) {
    adc_data_t *src = get_adc_data(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(adc_data_t));
        release_adc_data();
        return true;
    } else {
        return false;
    }
}

static inline void adc_spi(uint8_t *a, size_t len) {
    spi_dma_move_array(ADC_SPI, a, len);
}

static inline void adc_assert(void) {
    clear_pin(CS_ADC_PORT, CS_ADC_PIN);
}

static inline void adc_deassert(void) {
    set_pin(CS_ADC_PORT, CS_ADC_PIN);
}

bool init_adc(void) {
    _adcDataMutex = xSemaphoreCreateMutex();
    configASSERT(_adcDataMutex);
    memset(&_adcData, 0, sizeof(adc_data_t));

    mcp356x_error_t err;

    _adc = mcp356x_init(adc_spi, adc_assert, adc_deassert);

    _adc.config.VREF_SEL = VREF_SEL_EXT;
    _adc.config.CLK_SEL = CLK_SEL_INT;
    _adc.config.ADC_MODE = ADC_MODE_CONV;
    _adc.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    _adc.config.DATA_FORMAT = DATA_FORMAT_32_SGN;
    _adc.config.CRC_FORMAT = CRC_FORMAT_16;
    _adc.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    _adc.config.EN_STP = 0;
    _adc.config.OSR = OSR_4096;
    _adc.config.EN_CRCCOM = 1;


    err = mcp356x_reset(&_adc);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("Current adc init failed!\n");
        return false;
    }

    err = mcp356x_set_config(&_adc);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("Current adc init failed!\n");
        return false;
    }

    xTaskCreate(adc_task, "adc", ADC_TASK_STACK, NULL, ADC_TASK_PRIO, NULL);
    return true;
}


static void adc_task(void *p) {
    (void) p;

    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(25);
    xLastWakeTime = xTaskGetTickCount();
    int32_t currentVal = 0;
    int32_t ubatVal = 0;
    int32_t ulinkVal = 0;
    float ubatVolt = 0.0f;
    float ulinkVolt = 0.0f;

    bool adcError = false;

    while (1) {

        // If an error occurred, try to reset the adc to clear the error
//        if (adcError) {
//            mcp356x_reset(&_adc);
//            mcp356x_set_config(&_adc);
//        }
//
//
//        if (mcp356x_acquire(&_adc, MUX_CH4, MUX_CH5) != MCP356X_ERROR_OK) {
//            adcError = true;
//        }
//        vTaskDelay(pdMS_TO_TICKS(6));
//        if (mcp356x_read_value(&_adc, &currentVal, NULL, NULL) == MCP356X_ERROR_OK) {
//            adcError = false;
//        } else {
//            adcError = true;
//        }


        if (mcp356x_acquire(&_adc, MUX_CH3, MUX_CH2) != MCP356X_ERROR_OK) {
            adcError = true;
        }
        vTaskDelay(pdMS_TO_TICKS(6));
        if (mcp356x_read_value(&_adc, &ubatVal, NULL, NULL) == MCP356X_ERROR_OK) {
            adcError = false;
        } else {
            adcError = true;
        }

        if (mcp356x_acquire(&_adc, MUX_CH1, MUX_CH0) != MCP356X_ERROR_OK) {
            adcError = true;
        }
        vTaskDelay(pdMS_TO_TICKS(6));
        if (mcp356x_read_value(&_adc, &ulinkVal, NULL, NULL) == MCP356X_ERROR_OK) {
            adcError = false;
        } else {
            adcError = true;
        }

        ubatVolt = ADC_VOLTAGE_CONVERSION_RATIO * (ubatVal * 2.5f) / 8388608.0f;
        ubatVolt = (ubatVolt - (1.3606f / 0.99671f)) * 0.99671f;

        ulinkVolt = ADC_VOLTAGE_CONVERSION_RATIO * (ulinkVal * 2.5f) / 8388608.0f;
        ulinkVolt = (ulinkVolt + (1.11091f / 0.989208f)) * 0.989208f;


        PRINTF("%.3f\n", ulinkVolt);

//        PRINTF("Ubat cal: %f\n", ubatCal);

//        if (counter >= 100) {
//            counter = 0;
//            accu = accu / 100.0;
//            PRINTF("%.3f\n", accu);
//            accu = 0;
//        } else {
//            counter++;
//            accu = accu + ulinkVolt;
//        }

//        PRINTF("%f\n", counter, ubatVolt);



        if (!adcError) {
//            PRINTF("I: %.2f A\n", current);
        } else {
            PRINTF("ADC Error!\n");
        }


        adc_data_t *adcData = get_adc_data(pdMS_TO_TICKS(4));
        if (adcData != NULL) {
            adcData->batteryVoltage = ubatVolt;
            adcData->dcLinkVoltage = ulinkVolt;
            adcData->valid = !adcError;
            release_adc_data();
        } else {
            PRINTF("adc: Can't get mutex!\n");
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
