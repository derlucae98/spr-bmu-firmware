#ifndef SAFETY_H_
#define SAFETY_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "LTC6811.h"
#include "gpio.h"
#include "gpio_def.h"

void shutdown_circuit_handle_task(void *p);

#endif
