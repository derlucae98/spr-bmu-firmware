/*
 * eeprom.c
 *
 *  Created on: Feb 5, 2022
 *      Author: Luca Engelmann
 */


//Lib designed for AT25M02 2 MBit EEPROM
#include "eeprom.h"

#define CMD_WRSR       0x01
#define CMD_WREN       0x06
#define CMD_WRITE      0x02
#define CMD_READ       0x03
#define CMD_RDSR       0x05
#define CMD_LPWP       0x08

#define SR_WPEN       (0x01 << 7)
#define SR_BP_NONE    (0)
#define SR_BP_QUARTER (0x01 << 2)
#define SR_BP_HALF    (0x02 << 2)
#define SR_BP_FULL    (0x03 << 2)
#define SR_WEL        (0x01 << 1)
#define SR_BUSY        0x01

static inline void assert_cs(void);
static inline void deassert_cs(void);
static void set_status_register(uint8_t val);
static void set_write_enable_latch(void);

void eeprom_write_page(uint8_t *data, size_t startAddress, size_t len) {
    if (spi_mutex_take(EEPROM_SPI, pdMS_TO_TICKS(1000))) {
        uint8_t cmd[4];
        cmd[0] = CMD_WRITE;
        cmd[1] = (startAddress >> 16) & 0xFF;
        cmd[2] = (startAddress >> 8) & 0xFF;
        cmd[3] = startAddress & 0xFF;
        set_status_register(SR_BP_QUARTER | SR_WEL);
        set_write_enable_latch();
        assert_cs();
        spi_move_array(EEPROM_SPI, cmd, 4);
        spi_move_array(EEPROM_SPI, data, len);
        deassert_cs();
        spi_mutex_give(EEPROM_SPI);
    }
}

void eeprom_read_array(uint8_t *data, size_t startAddress, size_t len) {
    if (spi_mutex_take(EEPROM_SPI, pdMS_TO_TICKS(1000))) {
        uint8_t cmd[4];
        cmd[0] = CMD_READ;
        cmd[1] = (startAddress >> 16) & 0xFF;
        cmd[2] = (startAddress >> 8) & 0xFF;
        cmd[3] = startAddress & 0xFF;
        assert_cs();
        spi_move_array(EEPROM_SPI, cmd, 4);
        spi_move_array(EEPROM_SPI, data, len);
        deassert_cs();
        spi_mutex_give(EEPROM_SPI);
    }
}

eeprom_status_t eeprom_get_status(void) {
    if (spi_mutex_take(EEPROM_SPI, pdMS_TO_TICKS(1000))) {
        uint8_t cmd[2];
        cmd[0] = CMD_RDSR;
        assert_cs();
        spi_move_array(EEPROM_SPI, cmd, 2);
        deassert_cs();
        return cmd[1];
        spi_mutex_give(EEPROM_SPI);
    }
    return eeprom_comm_error;
}

bool eeprom_has_write_finished(void) {
    if (spi_mutex_take(EEPROM_SPI, pdMS_TO_TICKS(1000))) {
        uint8_t cmd[2];
        cmd[0] = CMD_LPWP;
        assert_cs();
        spi_move_array(EEPROM_SPI, cmd, 2);
        deassert_cs();
        spi_mutex_give(EEPROM_SPI);

        if (cmd[1] == 0) {
            return true;
        }
    }
    return false;
}

static inline void assert_cs(void) {
    clear_pin(CS_EEPROM_PORT, CS_EEPROM_PIN);
}

static inline void deassert_cs(void) {
    set_pin(CS_EEPROM_PORT, CS_EEPROM_PIN);
}

static void set_status_register(uint8_t val) {
    uint8_t cmd[2];
    cmd[0] = CMD_WRSR;
    cmd[1] = val;
    assert_cs();
    spi_move_array(EEPROM_SPI, cmd, 2);
    deassert_cs();
}

static void set_write_enable_latch(void) {
    uint8_t val = CMD_WREN;
    assert_cs();
    spi_move_array(EEPROM_SPI, &val, 1);
    deassert_cs();
}
