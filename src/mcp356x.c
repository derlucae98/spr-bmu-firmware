/*
 * mcp356x.c
 *
 *  Created on: Jan 31, 2022
 *      Author: Luca Engelmann (derlucae98)
 */

#include "mcp356x.h"

#define COMMAND_RESET 0x78
#define COMMAND_ADC_FAST_START 0x68
#define COMMAND_ADC
#define INCR_WRITE(x) (0x42 | ((x & 0xF) << 2))
#define INCR_READ(x) (0x43 | ((x & 0xF) << 2))
#define STATIC_READ(x)  (0x41 | ((x & 0xF) << 2))
#define CONFIG0 (0xE0)
#define ADC_STBY 0x2
#define ADC_CONV 0x3
#define REG_ADCDATA 0x0
#define REG_CONFIG0 0x1
#define REG_CONFIG1 0x2
#define REG_CONFIG2 0x3
#define REG_CONFIG3 0x4
#define REG_IRQ 0x5
#define REG_MUX     0x6

#define ADC_CONV_COMPLETE 0x13

extern void PRINTF(const char *format, ...);

mcp356x_obj_t mcp356x_init(mcp356x_spi_move_array_t mcp356x_spi_move_array,
                           mcp356x_assert_cs_t mcp356x_assert_cs,
                           mcp356x_deassert_cs_t mcp356x_deassert_cs) {

    mcp356x_obj_t obj;
    obj.spi_move_array = mcp356x_spi_move_array;
    obj.assert_cs = mcp356x_assert_cs;
    obj.deassert_cs = mcp356x_deassert_cs;
    return obj;
}

mcp356x_error_t mcp356x_reset(mcp356x_obj_t *obj) {
    if (!obj) {
        return MCP356X_ERROR_FAILED;
    }
    uint8_t cmd_reset = COMMAND_RESET;
    obj->assert_cs();
    obj->spi_move_array(&cmd_reset, 1);
    obj->deassert_cs();
}


mcp356x_error_t mcp356x_set_config(mcp356x_obj_t *obj, mcp356x_config_t cfg) {
    if (!obj) {
        return MCP356X_ERROR_FAILED;
    }

    //We send the configuration all at once using incremental write
    uint8_t buf[6];
    memset(buf, 0, sizeof(buf));
    buf[0] = INCR_WRITE(REG_CONFIG0);
    buf[1] = (cfg.VREF_SEL << 7U) | (cfg.CLK_SEL << 4U) | (cfg.CS_SEL << 2U) | (cfg.ADC_MODE);
    buf[2] = (cfg.PRE << 6U) | (cfg.OSR << 2U);
    buf[3] = (cfg.BOOST << 6U) | (cfg.GAIN << 3U) | (cfg.AZ_MUX << 2U) | (cfg.AZ_REF << 1U);
    buf[4] = (cfg.CONV_MODE << 6U) | (cfg.DATA_FORMAT << 4U) | (cfg.CRC_FORMAT << 3U)
           | (cfg.EN_CRCCOM << 2U) | (cfg.EN_OFFCAL << 1U) | (cfg.EN_GAINCAL);
    buf[5] = (cfg.IRQ_MODE << 2U) | (cfg.EN_FASTCMD << 1U) | (cfg.EN_STP);

    obj->assert_cs();
    obj->spi_move_array(buf, sizeof(buf));
    obj->deassert_cs();

    //00010xxx
    //Check status byte and look for the default device address
//    buf[0] &= 0x10;
    (void)buf[0];
    if (!(buf[0]^0x10)) {
        return MCP356X_ERROR_OK;
    } else {
        return MCP356X_ERROR_FAILED;
    }
}

mcp356x_error_t mcp356x_get_value(mcp356x_obj_t *obj, mcp356x_channel_t ch_pos, mcp356x_channel_t ch_neg, uint32_t *val) {
    if (!obj) {
        return MCP356X_ERROR_FAILED;
    }
    //Set channel
    uint8_t buf[2];
    buf[0] = INCR_WRITE(REG_MUX);
    buf[1] = ((ch_pos & 0x0F) << 4) | (ch_neg & 0x0F);
    obj->assert_cs();
    obj->spi_move_array(buf, 2);
    obj->deassert_cs();

    //Request conversion
    uint8_t cmd = COMMAND_ADC_FAST_START;
    obj->assert_cs();
    obj->spi_move_array(&cmd, 1);
    obj->deassert_cs();

    //Poll ADC value
    uint8_t status = 0;
    cmd = STATIC_READ(REG_ADCDATA);
    while (status != ADC_CONV_COMPLETE) {
        obj->assert_cs();
        obj->spi_move_array(&cmd, 1);
        status = cmd;
        cmd = STATIC_READ(REG_ADCDATA);
        obj->deassert_cs();
    }

    //Read ADC value
    cmd = STATIC_READ(REG_ADCDATA);
    uint8_t rec[5];
    memset(rec, 0xFF, sizeof(rec));
    obj->assert_cs();
    obj->spi_move_array(&cmd, 1);
    obj->spi_move_array(rec, 5);
    obj->deassert_cs();

    uint32_t res = ((uint32_t)rec[0] << 16) | ((uint32_t)rec[1] << 8) | ((uint32_t)rec[2]);
    float volt = res * 2.976656E-7;
    PRINTF("%.3f V\n", volt);

    return MCP356X_ERROR_OK;
}
