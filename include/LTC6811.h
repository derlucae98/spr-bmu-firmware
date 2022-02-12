#ifndef LTC6811_H_
#define LTC6811_H_

#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "S32K146.h"
#include "spi.h"
#include "uart.h"
#include "gpio.h"
#include "gpio_def.h"
#include "config.h"

#ifndef NUMBEROFSLAVES
#error "NUMBEROFSLAVES undefined!"
#endif

#ifndef CELL_UNDERVOLTAGE
#error "CELL_UNDERVOLTAGE undefined!"
#endif

#ifndef CELL_OVERVOLTAGE
#error "CELL_OVERVOLTAGE undefined!"
#endif

#ifndef MAXSTACKS
#error "MAXSTACKS undefined!"
#endif

#ifndef MAXCELLS
#error "MAXCELLS undefined!"
#endif

#ifndef MAXTEMPSENS
#error "MAXTEMPSENS undefined!"
#endif

#ifndef MAXCELLTEMP
#error "MAXCELLTEMP undefined!"
#endif

/*!
 * @enum Enumeration type for possible errors.
 */
typedef enum {
    NOERROR         = 0x0, //!< NOERROR
    PECERROR        = 0x1, //!< PECERROR
    VALUEOUTOFRANGE = 0x2, //!< VALUEOUTOFRANGE
    OPENCELLWIRE    = 0x3, //!< OPENCELLWIRE
} LTCError_t;

void ltc6811_init(uint32_t UID[]);

/*!
 * @brief Wakes a daisy chain of _numberOfSlaves slaves
 */
void ltc6811_wake_daisy_chain(void);

/*!
 * @brief Performs the ADC conversion on all cells of all slaves and stores the result in an array
 * @param voltage Array in which the voltages will be stored.\n
 * An error code is stored in the lower byte if an error occurs
 */
void ltc6811_get_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]);

/*!
 * @brief Selects a mux channel, performs the ADC conversion and returns the temperature\n
 * in degree celsius as fixed point integer value with one decimal place.
 * @param temperature Array in which the temperatures will be stored.
 */
void ltc6811_get_temperatures_in_degC(uint16_t temperature[][MAXTEMPSENS], uint8_t temperatureStatus[][MAXTEMPSENS]);

/*!
 * @brief Gets the 32 bit unique ID stored in an EEPROM on the slave for every slave
 * @param UID Array in which the UIDs will be stored.\n
 * An error code is stored in the lower byte if an error occurs
 */
void ltc6811_get_uid(uint32_t UID[]);

/*!
 * @brief Performs an open wire check on the all slaves.
 * @param result Array in which the results will be stored. See enum LTCError_t for possible values.
 */
void ltc6811_open_wire_check(uint8_t result[][MAXCELLS+1]);

/*!
 *  @brief Enables/disables the desired balancing gates on the slaves.
 *  @param gates Array for the values of all 12*12 gates (0 = off, else on)
 */
void ltc6811_set_balancing_gates(uint8_t gates[][MAXCELLS]);






#endif /* LTC6811_H_ */
