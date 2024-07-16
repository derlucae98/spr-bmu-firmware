#ifndef CLOCK_H_
#define CLOCK_H_

#include "S32K14x.h"
#include "FreeRTOS.h"

void clock_init(void);
void SystemCoreClockUpdate(void);

#endif /* CLOCK_H_ */
