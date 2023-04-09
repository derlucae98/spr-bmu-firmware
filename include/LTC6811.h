/*!
 * @file            LTC6811.h
 * @brief           Library for the LTC6811 battery stack monitor
 *                  This Library is designed for a specific hardware.
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

#ifndef LTC6811_H_
#define LTC6811_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>


/*! @def NTC_CELL
 *  Defines the B-value for the NTCs used for the cell temperature monitoring
 */
#define NTC_CELL 3435

/*! @def NUMBEROFSLAVES
 *  Defines the number of devices in a the daisy-chain
 */
#define NUMBEROFSLAVES 12

/*! @def MAX_NUM_OF_CELLS
 *  Defines the number of cells per stack.
 */
#define MAX_NUM_OF_CELLS  12

/*! @def MAX_NUM_OF_TEMPSENS
 *  Defines the number of temperature sensors per stack.
 */
#define MAX_NUM_OF_TEMPSENS 6


/*! @typedef ltc_spi_move_array_t
 *  @brief Function pointer declaration for the SPI transceive function.
 *  @param a Pointer to the array to send. It will hold the received data afterwards.
 *  @param len Length of the array to send.
 */
typedef void (*ltc_spi_move_array_t)(uint8_t *a, size_t len);

/*! @typedef ltc_assert_cs_t
 *  @brief Function pointer declaration for the cs assert function.
 */
typedef void (*ltc_assert_cs_t)(void);

/*! @typedef ltc_deassert_cs_t
 *  @brief Function pointer declaration for the cs de-assert function.
 */
typedef void (*ltc_deassert_cs_t)(void);

/*! @typedef ltc_mutex_take_t
 *  @brief Function pointer declaration for the Mutex take function.
 *  @param blocktime Timeout for the OS to wait for the Mutex to become available.
 *  This function blocks on the Mutex to securely use the SPI peripheral.
 */
typedef bool (*ltc_mutex_take_t)(TickType_t blocktime);

/*! @typedef ltc_mutex_give_t
 *  @brief Function pointer declaration for the Mutex give function.
 *  This function returns the Mutex after the work is done.
 */
typedef void (*ltc_mutex_give_t)(void);

/*!
 * @enum Enumeration type for possible errors.
 */
typedef enum {
    NOERROR         = 0x0, //!< No error occurred
    PECERROR        = 0x1, //!< PEC error occurred
    VALUEOUTOFRANGE = 0x2, //!< Value is out of range
    OPENCELLWIRE    = 0x3, //!< Open wire
} LTCError_t;

/*! @brief Initializes the LTC6811 library.
 *  Provide hardware-dependant callback functions.
 *  @note All other functions require the callbacks set by this function!
 *  @param ltc_mutex_take Callback for the Mutex take function. @see ltc_mutex_take_t for parameters.
 *  @param ltc_murex_give Callback for the Mutex give function. @see ltc_mutex_give_t for parameters.
 *  @param ltc_spi_move_array Callback for the SPI transceive function. @see ltc_spi_move_array_t for parameters.
 *  @param ltc_assert_cs Callback for the chip-select assert function. @see ltc_assert_cs_t for parameters.
 *  @param ltc_deassert_cs Callback for the chip-select de-assert function. @see ltc_deassert_cs_t for parameters.
 *
 */
void ltc6811_init(ltc_mutex_take_t     ltc_mutex_take,
                  ltc_mutex_give_t     ltc_mutex_give,
                  ltc_spi_move_array_t ltc_spi_move_array,
                  ltc_assert_cs_t      ltc_assert_cs,
                  ltc_deassert_cs_t    ltc_deassert_cs);

/*! @brief Wakes a daisy-chain of NUMBEROFSLAVES devices.
 */
void ltc6811_wake_daisy_chain(void);

/*! @brief Get the cell voltage of NUMBEROFSLAVES devices.
 *  @param voltage Array for the voltages. Provide an array with the size of [NUMBEROFSLAVES][MAX_NUM_OF_CELLS] elements. The voltages are fixed-point values with 100uV resolution.
 *  @param voltageStatus Array for the status of each voltage measurement. Provide an array with the size of [NUMBEROFSLAVES][MAX_NUM_OF_CELLS] elements.
 *  @see LTCError_t.
 */
void ltc6811_get_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], LTCError_t voltageStatus[][MAX_NUM_OF_CELLS]);

/*! @brief Get the temperatures in degrees Celsius.
 *  Two temperatures are acquired simultaneously. The sensors are multiplexed.
 *  @param temperature Array for the temperatures. Provide an array of size [NUMBEROFSLAVES][MAX_NUM_OF_TEMPSENS]. The values are fixed-point values with 0.1°C resolution
 *  @param temperatureStatus Array for the status of each temperature measurement. Provide an array of size [NUMBEROFSLAVES][MAX_NUM_OF_TEMPSENS].
 *  @param startChannel First channel for each mux.
 *  @param len Number of channels to sample for each mux.
 *  @see LTCError_t.
 */
void ltc6811_get_temperatures_in_degC(uint16_t temperature[][MAX_NUM_OF_TEMPSENS],
                                      LTCError_t temperatureStatus[][MAX_NUM_OF_TEMPSENS],
                                      uint8_t startChannel,
                                      uint8_t len);

/*! @brief Get the unique ID of NUMBEROFSLAVES slaves.
 *  @param UID This parameter holds the 32-bit unique ID. Provide an array with the size of [NUMBEROFSLAVES] elements.
 */
void ltc6811_get_uid(uint32_t UID[]);

/*! @brief Performs an open wire check for the cell voltage measurement.
 *  For N cells, N+1 cell wires have to be checked.
 *  @param result This array holds the status of all cell wires. Provide an array with the size of [NUMBEROFSLAVES][MAX_NUM_OF_CELLS+1] elements.
 *  @see LTCError_t
 */
void ltc6811_open_wire_check(LTCError_t result[][MAX_NUM_OF_CELLS+1]);

/*! @brief Enable the balancing for specific cells
 *  @param gates This array holds the enable bit for every stack and slave.
 *      Provide an array with the size of [NUMBEROFSLAVES][MAX_NUM_OF_CELLS] elements.
 *      0 = balancing disabled. 1 = balancing enabled.
 */
void ltc6811_set_balancing_gates(uint8_t gates[][MAX_NUM_OF_CELLS]);

#endif /* LTC6811_H_ */
