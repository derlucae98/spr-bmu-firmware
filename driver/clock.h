#ifndef CLOCK_H_
#define CLOCK_H_

#include "S32K146.h"
#include "FreeRTOS.h"

void clock_init(void);
void SystemCoreClockUpdate(void);

#endif /* CLOCK_H_ */
