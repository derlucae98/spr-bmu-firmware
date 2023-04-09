/*
 * sensors.c
 *
 *  Created on: Feb 4, 2022
 *      Author: Luca Engelmann
 */

#include "adc.h"

static mcp356x_obj_t _adc;
static volatile adc_calibration_t _cal;

static SemaphoreHandle_t _adcDataMutex = NULL;
static adc_data_t _adcData;

extern void PRINTF(const char *format, ...);
static void adc_task(void *p);
static void adc_irq_callback(BaseType_t *higherPrioTaskWoken);
static TaskHandle_t adcTaskHandle = NULL;


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
    spi_move_array(ADC_SPI, a, len);
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

    attach_interrupt(IRQ_ADC_PORT, IRQ_ADC_PIN, IRQ_EDGE_FALLING, adc_irq_callback);

    mcp356x_error_t err;

    _adc = mcp356x_init(adc_spi, adc_assert, adc_deassert);

    _adc.config.VREF_SEL = VREF_SEL_EXT;
    _adc.config.CLK_SEL = CLK_SEL_INT;
    _adc.config.ADC_MODE = ADC_MODE_CONV;
    _adc.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    _adc.config.DATA_FORMAT = DATA_FORMAT_32_SGN_CHID;
    _adc.config.CRC_FORMAT = CRC_FORMAT_16;
    _adc.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    _adc.config.EN_STP = 0;
    _adc.config.OSR = OSR_512;
    _adc.config.AZ_MUX = 1;
    _adc.config.EN_CRCCOM = 0;
    _adc.config.SCAN = SCAN_DIFF_CH0_CH1 | SCAN_DIFF_CH2_CH3 | SCAN_DIFF_CH4_CH5 | SCAN_SE_CH4;
    _adc.config.DLY = SCAN_DLY_512;
    _adc.config.TIMER = 512;


    err = mcp356x_reset(&_adc);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("Adc reset failed!\n");
        return false;
    }

    err = mcp356x_set_config(&_adc);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("Adc set config failed!\n");
        return false;
    }

    xTaskCreate(adc_task, "adc", ADC_TASK_STACK, NULL, ADC_TASK_PRIO, &adcTaskHandle);
    return true;
}

static void adc_irq_callback(BaseType_t *higherPrioTaskWoken) {
    dbg1(1);
    vTaskNotifyGiveFromISR(adcTaskHandle, higherPrioTaskWoken);
}

static void adc_task(void *p) {
    (void) p;
    int32_t adcVal = 0;

    float ubatVolt = 0.0f;
    float ulinkVolt = 0.0f;
    float current = 0.0f;

    bool adcError = false;

    uint8_t chID;
    uint8_t sgn;

    while (1) {

        /* Sample period ~ 3ms for each value
           Each value is updated every 12ms
        */
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);


        if (mcp356x_read_value(&_adc, &adcVal, &sgn, &chID) == MCP356X_ERROR_OK) {
            adcError = false;
        } else {
            adcError = true;
        }

        mcp356x_scan_t scanCh = 1 << chID;
        switch ((mcp356x_scan_t)scanCh) {

        case SCAN_DIFF_CH0_CH1: //DC-Link / TSAC Output voltage
            ulinkVolt = ADC_VOLTAGE_CONVERSION_RATIO * (adcVal * -2.5f) / 8388608.0f;
            ulinkVolt = (ulinkVolt + (1.11091f / 0.989208f)) * 0.989208f;
            //PRINTF("U_Link: %.1f\n", ulinkVolt);
            break;
        case SCAN_DIFF_CH2_CH3: //Battery Voltage
            ubatVolt = ADC_VOLTAGE_CONVERSION_RATIO * (adcVal * -2.5f) / 8388608.0f;
            ubatVolt = (ubatVolt - (1.3606f / 0.99671f)) * 0.99671f;
            //PRINTF("U_Batt: %.1f\n", ubatVolt);
            break;
        case SCAN_DIFF_CH4_CH5: //Current
            //Todo
            break;
        case SCAN_SE_CH4: //Current open wire check
            //Todo
            break;
        default:
            adcError = true;
            break;
        }

        adc_data_t *adcData = get_adc_data(pdMS_TO_TICKS(4));
        if (adcData != NULL) {
            adcData->batteryVoltage = ubatVolt;
            adcData->dcLinkVoltage = ulinkVolt;
            adcData->current = current;
            adcData->valid = !adcError;
            release_adc_data();
        } else {
            PRINTF("adc: Can't get mutex!\n");
        }
        dbg1(0);
    }
}
