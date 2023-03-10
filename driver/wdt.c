/*
 * wdt.c
 *
 *  Created on: Aug 13, 2022
 *      Author: scuderia
 */

#include "wdt.h"

void init_wdt (void) {
    taskENTER_CRITICAL();
    WDOG->CNT = 0xD928C520; //unlock watchdog
    while((WDOG->CS & WDOG_CS_ULK_MASK)); //wait until registers are unlocked
    WDOG->TOVAL = 140; //set timeout value
    WDOG->CS = WDOG_CS_EN(1) | WDOG_CS_CLK(1) | WDOG_CS_CMD32EN(1) |WDOG_CS_PRES(1);
    while((WDOG->CS & WDOG_CS_RCS_MASK) == 0); //wait until new configuration takes effect
    taskEXIT_CRITICAL();
}

void refresh_wdt (void) {
    taskENTER_CRITICAL();
    WDOG->CNT = 0xB480A602; // refresh watchdog
    taskEXIT_CRITICAL();
}

void disable_wdt (void) {
  WDOG->CNT = 0xD928C520;
  WDOG->TOVAL = 0x0000FFFF;
  WDOG->CS = 0x00002100;
}
