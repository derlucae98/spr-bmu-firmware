/*!
 * @file            adc.c
 * @brief           This module handles the interface to the MCP3564 ADC.
 *                  The values are provided via a Mutex protected global variable.
 *                  The calibration values are stored in the hardware EEPROM.
 */

/*
Copyright 2023 Luca Engelmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "adc.h"

static mcp356x_obj_t prvAdc;

static SemaphoreHandle_t prvAdcDataMutex = NULL;
static adc_data_t prvAdcData;
static adc_calibration_t prvCal;

static void prv_adc_task(void *p);
static void prv_adc_irq_callback(BaseType_t *higherPrioTaskWoken);
static void prv_get_adc_calibration(void);
static TaskHandle_t prvAdcTaskHandle = NULL;
static adc_new_data_hook_t prv_adc_new_data_hook = NULL;

static void prv_adc_print_data(void *p);

static BaseType_t prv_adc_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(prvAdcDataMutex, blocktime);
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

static void prv_get_adc_calibration(void) {
    prvCal = get_adc_calibration();
}


bool init_adc(adc_new_data_hook_t adc_new_data_hook) {
    prv_adc_new_data_hook = adc_new_data_hook;

    prvAdcDataMutex = xSemaphoreCreateMutex();
    configASSERT(prvAdcDataMutex);
    memset(&prvAdcData, 0, sizeof(adc_data_t));

    mcp356x_error_t err;

    init_calibration();
    prv_get_adc_calibration();

    prvAdc = mcp356x_init(adc_spi, adc_assert, adc_deassert);

    prvAdc.config.VREF_SEL = VREF_SEL_EXT;
    prvAdc.config.CLK_SEL = CLK_SEL_INT;
    prvAdc.config.ADC_MODE = ADC_MODE_CONV;
    prvAdc.config.CONV_MODE = CONV_MODE_CONT_SCAN;
    prvAdc.config.DATA_FORMAT = DATA_FORMAT_32_SGN_CHID;
    prvAdc.config.CRC_FORMAT = CRC_FORMAT_16;
    prvAdc.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    prvAdc.config.EN_STP = 0;
    prvAdc.config.OSR = OSR_512;
    prvAdc.config.AZ_MUX = 1;
    prvAdc.config.EN_CRCCOM = 1;
    prvAdc.config.SCAN = SCAN_DIFF_CH0_CH1 | SCAN_DIFF_CH2_CH3 | SCAN_DIFF_CH4_CH5 | SCAN_SE_CH4;
    prvAdc.config.DLY = SCAN_DLY_512;
    prvAdc.config.TIMER = 512;

    err = mcp356x_reset(&prvAdc);

    if (err != MCP356X_ERROR_OK) {
        PRINTF("Adc reset failed!\n");
        return false;
    }

    err = mcp356x_set_config(&prvAdc);
    if (err != MCP356X_ERROR_OK) {
        PRINTF("Adc set config failed!\n");
        return false;
    }

    xTaskCreate(prv_adc_task, "adc", ADC_TASK_STACK, NULL, ADC_TASK_PRIO, &prvAdcTaskHandle);
    xTaskCreate(prv_adc_print_data, "", 500, NULL, 2, NULL);
    attach_interrupt(IRQ_ADC_PORT, IRQ_ADC_PIN, IRQ_EDGE_FALLING, prv_adc_irq_callback);
    return true;
}

adc_data_t* get_adc_data(TickType_t blocktime) {
    if (prv_adc_mutex_take(blocktime)) {
        return &prvAdcData;
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

void release_adc_data(void) {
    xSemaphoreGive(prvAdcDataMutex);
}

static void prv_adc_irq_callback(BaseType_t *higherPrioTaskWoken) {
    if (prvAdcTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(prvAdcTaskHandle, higherPrioTaskWoken);
    }
}

static void prv_adc_task(void *p) {
    (void) p;
    int32_t adcVal = 0;

    int32_t adcValUbatt = 0;
    int32_t adcValUlink = 0;
    int32_t adcValCurrent = 0;

    float adcValUbattCorr = 0.0f;
    float adcValUlinkCorr = 0.0f;
    float adcValCurrentCorr = 0.0f;

    float ubattVolt = 0.0f;
    float ulinkVolt = 0.0f;
    float current = 0.0f;

    bool adcError = false;
    bool currentValid = false;

    uint8_t chID;

    while (1) {

        /* Sample period ~ 3ms for each value
           Each value is updated every 12ms
        */
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        if (mcp356x_read_value(&prvAdc, &adcVal, NULL, &chID) == MCP356X_ERROR_OK) {
            adcError = false;
        } else {
            adcError = true;
        }

        mcp356x_scan_t scanCh = 1 << chID;
        switch ((mcp356x_scan_t)scanCh) {

        case SCAN_DIFF_CH0_CH1: //DC-Link / TSAC Output voltage
            adcValUlink = adcVal;
            break;
        case SCAN_DIFF_CH2_CH3: //Battery Voltage
            adcValUbatt = adcVal;
            break;
        case SCAN_DIFF_CH4_CH5: //Current
            adcValCurrent = adcVal;
            //Todo
            break;
        case SCAN_SE_CH4: //Current open wire check
            //Todo
            break;
        default:
            adcError = true;
            break;
        }

        //Update ADC values for the calibration module
        cal_update_adc_value(adcValUlink, adcValUbatt, adcValCurrent);

        if (get_cal_state() == CAL_STATE_FINISH) {
            //New calibration has been written
            //Reload local calibration values
            prv_get_adc_calibration();
        }

        //Apply calibration values
        adcValUlinkCorr   = prvCal.ulink_gain   * (adcValUlink   - prvCal.ulink_offset);
        adcValUbattCorr   = prvCal.ubatt_gain   * (adcValUbatt   - prvCal.ubatt_offset);
        adcValCurrentCorr = prvCal.current_gain * (adcValCurrent - prvCal.current_offset);

        ulinkVolt = ADC_VOLTAGE_CONVERSION_RATIO * (adcValUlinkCorr   * (-1) * prvCal.reference) / 8388608.0f; //*(-1) to compensate the inverting ADC driver
        ubattVolt = ADC_VOLTAGE_CONVERSION_RATIO * (adcValUbattCorr   * (-1) * prvCal.reference) / 8388608.0f; //*(-1) to compensate the inverting ADC driver
        current   = ADC_CURRENT_CONVERSION_RATIO * (adcValCurrentCorr * prvCal.reference) / 8388608.0f;

        adc_data_t newAdcData;
        newAdcData.batteryVoltage = ubattVolt;
        newAdcData.dcLinkVoltage = ulinkVolt;
        newAdcData.current = current;
        newAdcData.voltageValid = !adcError;
        newAdcData.currentValid = currentValid; //Todo: Validity for the current measurement has to be determined with the open wire check

        //The hook will provide the new data to a function, synchronous to the acquisition
        //Asynchronous access is possible using the access functions
        if (prv_adc_new_data_hook != NULL) {
            (prv_adc_new_data_hook)(newAdcData);
        }

        adc_data_t *adcData = get_adc_data(pdMS_TO_TICKS(2));
        if (adcData != NULL) {
            memcpy(adcData, &newAdcData, sizeof(adc_data_t));
            release_adc_data();
        } else {
            PRINTF("adc: Can't get mutex!\n");
            configASSERT(0);
        }
    }
}

static void prv_adc_print_data(void *p) {
    (void) p;

    TickType_t period = pdMS_TO_TICKS(200);
    TickType_t lastWake = xTaskGetTickCount();
    while (1) {
        adc_data_t adcData;
        memset(&adcData, 0, sizeof(adc_data_t));
        copy_adc_data(&adcData, portMAX_DELAY);
        PRINTF("U_Batt: %.1f\n", adcData.batteryVoltage);
        PRINTF("U_Link: %.1f\n", adcData.dcLinkVoltage);
        PRINTF("Current: %.1f\n", adcData.current);
        vTaskDelayUntil(&lastWake, period);
    }
}
