/*!
 * @file            eeprom.h
 * @brief           Library for interfacing with a AT25M02 or compatible EEPROM.
 */

/*
Copyright 2023 Luca Engelmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef EEPROM_H_
#define EEPROM_H_
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "gpio.h"
#include "config.h"
#include "spi.h"

#define EEPROM_SPI LPSPI2
#define EEPROM_PAGESIZE 256

/*!
 * @brief Write data to the EEPROM
 * @param data Array for the data to send
 * @param startAddress Start address of the EEPROM memory block
 * @param len Length of the data to be written
 * @param blocktime Specify a timeout in which the EEPROM and SPI Mutex must become available.
 * @return true on success, false on failure
 */
bool eeprom_write(uint8_t *data, size_t startAddress, size_t len, BaseType_t blocktime);

/*!
 * @brief Read data from the EEPROM
 * @param data Array for the data to be received
 * @param startAddress Start address of the EEPROM memory block
 * @param len Length of the data to be read
 * @param blocktime Specify a timeout in which the EEPROM and SPI Mutex must become available.
 * @return true on success, false on failure
 */
bool eeprom_read(uint8_t *data, size_t startAddress, size_t len, BaseType_t blocktime);

/*!
 * @brief Check if EEPROM is busy with writing
 * @param blocktime Specify a timeout in which the EEPROM and SPI Mutex must become available.
 * @return true if busy, false if ready
 */
bool eeprom_busy(BaseType_t blocktime);

/*!
 * @brief Calculate the CRC16-CCITT checksum
 * @param data Input data
 * @param len Input data length
 * @return Checksum
 */
uint16_t eeprom_get_crc16(uint8_t *data, size_t len);

/*!
 * @brief Check two checksums for equality
 * @param data Input data
 * @param len Input data length
 * @param crcIs Checksum to check against
 * @return true if equal, false if not equal
 */
bool eeprom_check_crc(uint8_t *data, size_t len, uint16_t crcIs);


#endif /* EEPROM_H_ */
