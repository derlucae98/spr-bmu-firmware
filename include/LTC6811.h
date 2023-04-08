#ifndef LTC6811_H_
#define LTC6811_H_

#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define NTC_CELL 3435
#define NUMBEROFSLAVES 1
#define MAXSTACKS 12
#define MAXCELLS  12
#define MAXTEMPSENS 14


typedef void (*ltc_spi_move_array_t)(uint8_t *a, size_t len);
typedef void (*ltc_assert_cs_t)(void);
typedef void (*ltc_deassert_cs_t)(void);
typedef bool (*ltc_mutex_take_t)(TickType_t blocktime);
typedef void (*ltc_mutex_give_t)(void);

/*!
 * @enum Enumeration type for possible errors.
 */
typedef enum {
    NOERROR         = 0x0, //!< NOERROR
    PECERROR        = 0x1, //!< PECERROR
    VALUEOUTOFRANGE = 0x2, //!< VALUEOUTOFRANGE
    OPENCELLWIRE    = 0x3, //!< OPENCELLWIRE
} LTCError_t;

void ltc6811_init(ltc_mutex_take_t ltc_mutex_take, ltc_mutex_give_t ltc_murex_give, ltc_spi_move_array_t ltc_spi_move_array, ltc_assert_cs_t ltc_assert_cs, ltc_deassert_cs_t ltc_deassert_cs);
void ltc6811_wake_daisy_chain(void);
void ltc6811_get_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]);
void ltc6811_get_temperatures_in_degC(uint16_t temperature[][MAXTEMPSENS], uint8_t temperatureStatus[][MAXTEMPSENS], uint8_t startChannel, uint8_t len);
void ltc6811_get_uid(uint32_t UID[]);
void ltc6811_open_wire_check(uint8_t result[][MAXCELLS+1]);
void ltc6811_set_balancing_gates(uint8_t gates[][MAXCELLS]);






#endif /* LTC6811_H_ */
