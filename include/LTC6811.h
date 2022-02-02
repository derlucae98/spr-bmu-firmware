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

#define LTC6811_SPI LPSPI0

#define MAXSTACKS 12
#define MAXCELLS  12
#define MAXTEMPSENS 14
#define MAXCELLTEMP 540 //54.0 deg C

typedef struct {
    uint32_t UID[MAXSTACKS];
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    uint16_t temperature[MAXSTACKS][MAXTEMPSENS];
    uint8_t temperatureStatus[MAXSTACKS][MAXTEMPSENS];
    uint16_t packVoltage;
    uint16_t current;
    uint8_t soc;
    bool bmsStatus;
    bool shutdownStatus;
} battery_data_t;

typedef struct {
    uint8_t numberOfSlaves;
    uint16_t undervoltageThreshold;
    uint16_t overvoltageThreshold;
    uint8_t workerTasksPrio;
} LTC_initial_data_t;

/*!
 * @enum Enumeration type for possible errors.
 */
typedef enum {
    NOERROR         = 0x0, //!< NOERROR
    PECERROR        = 0x1, //!< PECERROR
    VALUEOUTOFRANGE = 0x2, //!< VALUEOUTOFRANGE
    OPENCELLWIRE    = 0x3, //!< OPENCELLWIRE
} LTCError_t;

void ltc_init(LTC_initial_data_t initData);

void ltc_config_slaves_task(void *p);

void ltc6811_worker_task(void *p);
BaseType_t batteryData_mutex_take(TickType_t blocktime);
void batteryData_mutex_give(void);
extern battery_data_t batteryData;



#endif /* LTC6811_H_ */
