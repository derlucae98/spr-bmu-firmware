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
void ltc6811_wake_daisy_chain(void);
void ltc6811_get_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]);
void ltc6811_get_temperatures_in_degC(uint16_t temperature[][MAXTEMPSENS], uint8_t temperatureStatus[][MAXTEMPSENS]);
void ltc6811_get_uid(uint32_t UID[]);
void ltc6811_open_wire_check(uint8_t result[][MAXCELLS+1]);
void ltc6811_set_balancing_gates(uint8_t gates[][MAXCELLS]);






#endif /* LTC6811_H_ */
