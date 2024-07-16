/*
 * peripheral.h
 *
 *  Created on: Aug 24, 2023
 *      Author: luca
 */

#ifndef PERIPHERAL_H_
#define PERIPHERAL_H_
#include "FreeRTOS.h"
#include "semphr.h"


/*
 * Problem: SD card, RTC and EEPROM share the same SPI bus.
 * Writing to the card takes a relatively long time (~500ms) every second.
 * If the RTC or EEPROM try to access the bus while the card is in a write cycle,
 * the software crashes. An SPI mutex is not enough in this case because the write cycle envolves
 * several CS toggles (assumption, currently have no time to hook up the logic analyzer).
 * Idea to fix this issue:
 * Implement a peripheral mutex, which ensures that the write cycle of the card can not be interrupted.
 */

BaseType_t get_peripheral_mutex(TickType_t blocktime);
void release_peripheral_mutex(void);

#endif /* PERIPHERAL_H_ */
