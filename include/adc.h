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

typedef struct {
    float current;
    float batteryVoltage;
    float dcLinkVoltage;
    bool valid;
} adc_data_t;

typedef void (*adc_new_data_hook_t)(adc_data_t newData);

bool init_adc(adc_new_data_hook_t adc_new_data_hook);
adc_data_t* get_adc_data(TickType_t blocktime);
bool copy_adc_data(adc_data_t *dest, TickType_t blocktime);
void release_adc_data(void);






#endif /* ADC_H_ */
