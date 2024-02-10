
/*!
 * @file            adc_cal.h
 * @brief           Module which performs ADC calibration and interfaces with the EEPROM
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

#ifndef ADC_CAL_H_
#define ADC_CAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "eeprom.h"
#include "uart.h"
#include <stdbool.h>
#include <string.h>


/*!
 * @enum adc_cal_state_t
 * State of the internal state machine.
 */
typedef enum {
    CAL_STATE_STANDBY,              //!< State machine task is suspended
    CAL_STATE_WAIT_FOR_VALUE_1,     //!< State machine waits for the user to apply the first value (voltage or current) to the corresponding input
    CAL_STATE_WAIT_FOR_VALUE_2,     //!< State machine waits for the user to apply the second value (voltage or current) to the corresponding input
    CAL_STATE_UPDATE_CALIBRATION,   //!< New calibration values are calculated and stored to the EEPROM
    CAL_STATE_FINISH                //!< New calibration is stored in the EEPROM.
} adc_cal_state_t;

/*!
 * @enum adc_calibration_t
 * ADC calibration values.
 */
typedef struct {
    float reference;        //!< Voltage reference of the ADC
    float current_gain;     //!< Correction value for the gain error of the current measurement
    float current_offset;   //!< Correction value for the offset error of the current measurement
    float ubatt_gain;       //!< Correction value for the gain error of the battery voltage measurement
    float ubatt_offset;     //!< Correction value for the offset error of the battery voltage measurement
    float ulink_gain;       //!< Correction value for the gain error of the DC-Link voltage measurement
    float ulink_offset;     //!< Correction value for the offset error of the DC-Link voltage measurement
} adc_calibration_t;

/*!
 * @enum adc_cal_type_t
 * Used in start_calibration() and specifies, which ADC input is going to be calibrated.
 */
typedef enum {
    CAL_INPUT_UBATT_VOLT,    //!< Calibrate the battery voltage measurement
    CAL_INPUT_ULINK_VOLT,    //!< Calibrate the DC-Link voltage measurement
    CAL_INPUT_CURRENT        //!< Calibrate the current measurement
} adc_cal_input_t;

/*!
 * @brief Initialize the calibration module.
 * @note This is required for all other functions to work!
 */
void init_calibration(void);

/*!
 * @brief Start the calibration of the selected input.
 * This function will resume the calibration task and start the state machine.
 * After calling this function, the state machine will wait for the user to apply the first voltage/current value to the corresponding input.
 * Call value_applied() after the value has been applied.
 * @param input The ADc input to calibrate. See adc_cal_input_t
 */
void start_calibration(adc_cal_input_t input);

/*!
 * @brief Tells the state machine that the user has applied a voltage/current to the ADC input.
 * @param value The actual value that has been applied. This value should be measured with a precise multimeter.
 */
void value_applied(float value);

/*!
 * @brief Update the ADC value
 * This function is called from the ADC module, which acquires the ADC values.
 * The three values for each input are stored locally and are mandatory for the calibration algorithm.
 * @note Update each value within 20ms.
 * @param adcValUlink Raw ADC value of DC-Link voltage measurement
 * @param adcValUbatt Raw ADC value of battery voltage measurement
 * @param adcValCurrent Raw ADC value of current measurement
 */
void cal_update_adc_value(int32_t adcValUlink, int32_t adcValUbatt, int32_t adcValCurrent);

/*!
 * @brief Update the reference voltage of the ADC
 * If this function is not called, either the value stored in the EEPROM or the default value is used.
 * The new value will be stored in the EEPROM
 * @param voltage New reference voltage
 */
void set_adc_reference_voltage(float voltage);

/*!
 * @brief Switch the state machine state from finish to standby
 * After the calibration has finished, the internal state machine waits for the acknowledgment.
 * Calling this function will put the state machine into standby state and suspend the calibration task.
 */
void acknowledge_calibration(void);

/*!
 * @brief Return the current state of the internal state machine
 */
adc_cal_state_t get_cal_state(void);

/*!
 * @brief Return the calibration data stored in the EEPROM
 */
adc_calibration_t get_adc_calibration(void);


#endif
