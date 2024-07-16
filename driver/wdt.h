/*
 * wdt.h
 *
 *  Created on: Aug 13, 2022
 *      Author: scuderia
 */

#ifndef WDT_H_
#define WDT_H_

#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"

void init_wdt(void);
void refresh_wdt(void);
void disable_wdt (void);

#endif /* WDT_H_ */
