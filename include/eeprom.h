/*
 * eeprom.h
 *
 *  Created on: Feb 5, 2022
 *      Author: Luca Engelmann
 */

//Lib designed for AT25M02 2 MBit EEPROM

#ifndef EEPROM_H_
#define EEPROM_H_
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "S32K146.h"
#include "gpio.h"
#include "gpio_def.h"
#include "spi.h"

#define EEPROM_SPI LPSPI0
#define EEPROM_PAGESIZE 256

typedef enum {
    eeprom_comm_error,
    eeprom_write_pending,
    eeprom_write_finished
} eeprom_status_t;

void eeprom_write_page(uint8_t *data, size_t startAddress, size_t len);
void eeprom_read_array(uint8_t *data, size_t startAddress, size_t len);
eeprom_status_t eeprom_get_status(void);
bool eeprom_has_write_finished(void);
uint16_t eeprom_get_crc16(uint8_t *data, size_t len);
bool eeprom_check_crc(uint8_t *data, size_t len, uint16_t crcIs);


#endif /* EEPROM_H_ */
