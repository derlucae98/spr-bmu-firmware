/*!
 * @file            stacks.h
 * @brief           Module which acquires the stack values.
 *                  The values are provided via a Mutex protected global variable
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

#ifndef STACKS_H_
#define STACKS_H_

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "config.h"
#include "LTC6811.h"
#include "spi.h"
#include "uart.h"
#include "cal.h"
#include <stdbool.h>



/*!
 * @struct stacks_data_t
 * Data type for the stack values containing all cell voltages,
 * cell voltage status, cell temperatures, cell temperature status,
 * cell voltage and temperature statistics and the unique IDs for every slave.
 */
typedef struct {
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS+1];
    uint16_t temperature[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];
    uint8_t temperatureStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_TEMPSENS];

    uint16_t minCellVolt;
    uint16_t maxCellVolt;
    uint16_t avgCellVolt;
    bool voltageValid;
    uint16_t minTemperature;
    uint16_t maxTemperature;
    uint16_t avgTemperature;
    bool temperatureValid;

    uint32_t UID[MAX_NUM_OF_SLAVES];
} stacks_data_t;

/*!
 *  @brief Initializes the LTC6811 library and reads the unique ID.
 *  @note This function must be called before any other function in this module can be used!
 */
void init_stacks(void);

/*!
 *  @brief Global enable or disable for the cell balancing.
 *  @param enabled Set this value to true to enable the balancing. Set to false to disable balancing.
 */
void control_balancing(bool enabled);

/*!
 *  @brief Get the currently set balancing gates.
 *  The balancing algorithm is executed in this module.
 *  This function reports the currently set balancing gates for a
 *  visual feedback in the control software.
 *  @param gates Provide an array of size [NUMBEROFSLAVES][MAX_NUM_OF_CELLS] in which the data will be stored.
 */
void get_balancing_status(uint8_t gates[NUMBEROFSLAVES][MAX_NUM_OF_CELLS]);

/*!
 *  @brief Returns a pointer to the variable containing the stack values.
 *  @param blocktime Specify a timeout in which the Mutex must become available.
 *  @return Returns a pointer to the variable if the Mutex could be obtained. Otherwise NULL.
 *  @code
 *      //Example:
 *      stacks_data_t* stacksData = get_stacks_data(portMAX_DELAY);
        if (stacksData != NULL) {
            //Use stacksData
            release_stacks_data();
        }
 *  @endcode
 *  @note Always check the return value for NULL before dereferencing it!
 */
stacks_data_t* get_stacks_data(TickType_t blocktime);

/*!
 *  @brief Copy the global stacks_data variable to a local variable.
 *  This function works similar to get_stacks_data();
 *  @code
 *      //Example:
 *      stacks_data_t stacksDataLocal;
 *      copy_stacks_data(&stacksDataLocal, portMAX_DELAY);
 *  @endcode
 *  @param dest Pointer to the destination variable
 *  @param blocktime Specify a timeout in which the Mutex must become available.
 *  @return Returns true on success and false on failure.
 */
bool copy_stacks_data(stacks_data_t *dest, TickType_t blocktime);

/*!
 *  @brief Release the Mutex of the global variable.
 *  Call this function after accessing the variable.
 *  @see get_stacks_data()
 */
void release_stacks_data(void);

bool check_voltage_validity(uint8_t voltageStatus[][MAX_NUM_OF_CELLS+1], uint8_t stacks);
bool check_temperature_validity(uint8_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t stacks);


#endif /* STACKS_H_ */
