/*!
 * @file            adc.h
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

#ifndef ADC_H_
#define ADC_H_

#include "FreeRTOS.h"
#include "task.h"
#include "mcp356x.h"
#include "spi.h"
#include "gpio.h"
#include "config.h"
#include "uart.h"
#include "adc_cal.h"
#include <stdbool.h>
#include <math.h>

/*!
 * @struct adc_data_t
 * Datatype for the ADC values.
 * The DC-Link voltage, battery voltage, and current are provided as floating-point values.
 * The validity bits indicates the validity of the data. true = valid, false = invalid
 */
typedef struct {
    float current;
    float batteryVoltage;
    float dcLinkVoltage;
    bool currentValid;
    bool voltageValid;
} adc_data_t;

/*!
 * @typedef adc_new_data_hook_t
 * @brief Function pointer for a hook at new available data
 * The hook provides new ADC data synchronous to the acquisition of the ADC.
 * @note Keep the hook function as short as possible!
 * @param newData Updated data of type adc_data_t
 */
typedef void (*adc_new_data_hook_t)(adc_data_t newData);

/*!
 * @brief Initialize the ADC module
 * @param adc_new_data_hook Provide a hook function. See @ref adc_new_data_hook_t. It is safe to set this value to NULL if not used.
 * @return Returns true on success and false on failure
 */
bool init_adc(adc_new_data_hook_t adc_new_data_hook);


/*!
 *  @brief Returns a pointer to the variable containing the ADC values.
 *  @param blocktime Specify a timeout in which the Mutex must become available.
 *  @return Returns a pointer to the variable if the Mutex could be obtained. Otherwise NULL.
 *  @code
 *      //Example:
 *      adc_data_t* adcData = get_adc_data(portMAX_DELAY);
        if (adcData != NULL) {
            //Use adcData
            release_adc_data();
        }
 *  @endcode
 *  @note Always check the return value for NULL before dereferencing it!
 */
adc_data_t* get_adc_data(TickType_t blocktime);

/*!
 *  @brief Copy the global adc_data variable to a local variable.
 *  This function works similar to get_adc_data();
 *  @code
 *      //Example:
 *      adc_data_t adcDataLocal;
 *      copy_adc_data(&adcDataLocal, portMAX_DELAY);
 *  @endcode
 *  @param dest Pointer to the destination variable
 *  @param blocktime Specify a timeout in which the Mutex must become available.
 *  @return Returns true on success and false on failure.
 */
bool copy_adc_data(adc_data_t *dest, TickType_t blocktime);

/*!
 *  @brief Release the Mutex of the global variable.
 *  Call this function after accessing the variable.
 *  @see get_adc_data()
 */
void release_adc_data(void);


#endif /* ADC_H_ */
