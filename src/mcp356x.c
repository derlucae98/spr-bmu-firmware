/*
Copyright (c) 2022 Luca Engelmann (derlucae98)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "mcp356x.h"

#define COMMAND_RESET           0x78
#define COMMAND_ADC_FAST_START  0x68
#define INCR_WRITE(x)           (0x42 | ((x & 0xF) << 2))
#define INCR_READ(x)            (0x43 | ((x & 0xF) << 2))
#define STATIC_READ(x)          (0x41 | ((x & 0xF) << 2))
#define REG_ADCDATA             0x0
#define REG_CONFIG0             0x1
#define REG_CONFIG1             0x2
#define REG_CONFIG2             0x3
#define REG_CONFIG3             0x4
#define REG_IRQ                 0x5
#define REG_MUX                 0x6
#define ADC_CONV_COMPLETE       0x13
#define MCLK_INT                3300000UL //3.3 MHz min.

extern void PRINTF(const char *format, ...);

static const uint16_t crcLookup[256] = {
    0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011, 0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072, 0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
    0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2, 0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
    0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1, 0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192, 0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
    0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1, 0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
    0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151, 0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132, 0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312, 0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371, 0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
    0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1, 0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
    0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2, 0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291, 0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
    0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2, 0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
    0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252, 0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231, 0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
};

static uint16_t generate_crc16(const uint8_t *a, size_t len) {
    uint16_t crc = 0;
    while (len--)
        crc = (crc << 8) ^ crcLookup[((crc >> 8) ^ *a++)];
    return crc;
}

static bool check_crc(mcp356x_obj_t *obj, const uint8_t *a) {
    uint16_t crcIs;
    uint16_t crcShould;

    switch (obj->config.DATA_FORMAT) {
    case DATA_FORMAT_24:
        crcIs = (a[4] << 8) | a[5];
        crcShould = generate_crc16(a, 4);
        break;
    case DATA_FORMAT_32:
    case DATA_FORMAT_32_SGN:
    case DATA_FORMAT_32_SGN_CHID:
        crcIs = (a[5] << 8) | a[6];
        crcShould = generate_crc16(a, 5);
        break;
    default:
        return false;
    }

    if (crcIs != crcShould) {
        return false;
    }

    return true;
}

static float gain_lookup(mcp356x_obj_t *obj) {
    //Returns the value of the gain. See mcp356x_get_voltage()
    uint8_t gain = (1u << obj->config.GAIN);
    gain >>= 1;

    if (gain == 0) {
        return 0.33f;
    }

    return (float)gain;
}

static bool check_status(uint8_t status) {
    status &= 0xF8;
    if (status == 0x10) {
        return true;
    } else {
        return false;
    }
}

mcp356x_obj_t mcp356x_init(mcp356x_spi_move_array_t mcp356x_spi_move_array,
                           mcp356x_assert_cs_t mcp356x_assert_cs,
                           mcp356x_deassert_cs_t mcp356x_deassert_cs) {

    mcp356x_obj_t obj;
    obj.spi_move_array = mcp356x_spi_move_array;
    obj.assert_cs = mcp356x_assert_cs;
    obj.deassert_cs = mcp356x_deassert_cs;

    //Initialize config to default values
    obj.config.VREF_SEL = VREF_SEL_INT;
    obj.config.CLK_SEL = CLK_SEL_EXT;
    obj.config.CS_SEL = CS_SEL_OFF;
    obj.config.ADC_MODE = ADC_MODE_SDN;
    obj.config.PRE = PRE_1;
    obj.config.OSR = OSR_256;
    obj.config.BOOST = BOOST_1;
    obj.config.GAIN = GAIN_1;
    obj.config.AZ_MUX = 0;
    obj.config.AZ_REF = 1;
    obj.config.CONV_MODE = CONV_MODE_ONE_SHOT_SCAN_SDN;
    obj.config.DATA_FORMAT = DATA_FORMAT_24;
    obj.config.CRC_FORMAT = CRC_FORMAT_16;
    obj.config.EN_CRCCOM = 0;
    obj.config.EN_OFFCAL = 0;
    obj.config.EN_GAINCAL = 0;
    obj.config.IRQ_MODE = IRQ_MODE_IRQ_HIGH_Z;
    obj.config.EN_FASTCMD = 1;
    obj.config.EN_STP = 1;
    obj.config.MCLK = 0;
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

    if (check_status(cmd_reset)) {
        return MCP356X_ERROR_OK;
    } else {
        return MCP356X_ERROR_FAILED;
    }
}


mcp356x_error_t mcp356x_set_config(mcp356x_obj_t *obj) {
    if (!obj) {
        return MCP356X_ERROR_FAILED;
    }

    uint8_t buf[6];
    memset(buf, 0, sizeof(buf));
    buf[0] = INCR_WRITE(REG_CONFIG0);
    buf[1] = (obj->config.VREF_SEL << 7) | (obj->config.CLK_SEL << 4) | (obj->config.CS_SEL << 2) | (obj->config.ADC_MODE);
    buf[2] = (obj->config.PRE << 6) | (obj->config.OSR << 2);
    buf[3] = (obj->config.BOOST << 6) | (obj->config.GAIN << 3) | (obj->config.AZ_MUX << 2) | (obj->config.AZ_REF << 1);
    buf[4] = (obj->config.CONV_MODE << 6) | (obj->config.DATA_FORMAT << 4) | (obj->config.CRC_FORMAT << 3)
           | (obj->config.EN_CRCCOM << 2) | (obj->config.EN_OFFCAL << 1) | (obj->config.EN_GAINCAL);
    buf[5] = (obj->config.IRQ_MODE << 2) | (obj->config.EN_FASTCMD << 1) | (obj->config.EN_STP);

    obj->assert_cs();
    obj->spi_move_array(buf, sizeof(buf));
    obj->deassert_cs();

    if (check_status(buf[0])) {
        return MCP356X_ERROR_OK;
    } else {
        return MCP356X_ERROR_FAILED;
    }
}

mcp356x_error_t mcp356x_get_value(mcp356x_obj_t *obj, mcp356x_channel_t ch_pos, mcp356x_channel_t ch_neg, int32_t *val, uint8_t *sgn, uint8_t *chID) {
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

    if (!check_status(buf[0])) {
        return MCP356X_ERROR_FAILED;
    }

    //Request conversion
    uint8_t cmd = COMMAND_ADC_FAST_START;
    obj->assert_cs();
    obj->spi_move_array(&cmd, 1);
    obj->deassert_cs();

    if (!check_status(cmd)) {
        return MCP356X_ERROR_FAILED;
    }

    //Poll ADC value
    uint8_t status = 0;
    cmd = STATIC_READ(REG_ADCDATA);
    uint16_t timeout = 1000; //TODO: Implement timeout based on OSR and DMCLK (Table 5-6)
    while (status != ADC_CONV_COMPLETE) {
        obj->assert_cs();
        obj->spi_move_array(&cmd, 1);
        status = cmd;
        cmd = STATIC_READ(REG_ADCDATA);
        obj->deassert_cs();

        if (!check_status(status)) {
            //No valid answer at all
            return MCP356X_ERROR_FAILED;
        }

        if (--timeout == 0) {
            return MCP356X_ERROR_FAILED;
        }
    }

    //Read ADC value
    uint8_t rec[7];
    memset(rec, 0xFF, sizeof(rec));
    rec[0] = STATIC_READ(REG_ADCDATA);
    obj->assert_cs();
    obj->spi_move_array(rec, 7);
    obj->deassert_cs();

    if (!check_status(rec[0])) {
        return MCP356X_ERROR_FAILED;
    }

    if (!check_crc(obj, rec)) {
        return MCP356X_ERROR_CRC;
    }

    uint8_t sign = 0;
    if (val != NULL) {
        switch (obj->config.DATA_FORMAT) {
        case DATA_FORMAT_24:
        case DATA_FORMAT_32:
            sign = (rec[1] & 0x80) ? 0xFF : 0x0;
            *val = ((uint32_t)sign << 24) | ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | ((uint32_t)rec[3]);
            break;
        case DATA_FORMAT_32_SGN:
            sign = rec[1];
            *val = ((uint32_t)rec[1] << 24) | ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 8) | ((uint32_t)rec[4]);
            break;
        case DATA_FORMAT_32_SGN_CHID:
            sign = (rec[1] & 0x0F) ? 0xFF : 0x0;
            *val = ((uint32_t)sign << 24) | ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 8) | ((uint32_t)rec[4]);
            break;
        default:
            return MCP356X_ERROR_FAILED;
        }
    }

    if (sgn != NULL) {
        *sgn = sign & 0x01;
    }

    if (chID != NULL && obj->config.DATA_FORMAT == DATA_FORMAT_32_SGN_CHID) {
        *chID = (rec[1] >> 4);
    }

    return MCP356X_ERROR_OK;
}

mcp356x_error_t mcp356x_get_voltage(mcp356x_obj_t *obj, mcp356x_channel_t ch_pos, mcp356x_channel_t ch_neg, float refVoltage, float *result) {
    int32_t val;
    uint8_t sgn;
    uint8_t chID;
    mcp356x_error_t res;
    res = mcp356x_get_value(obj, ch_pos, ch_neg, &val, &sgn, &chID);

    if (res != MCP356X_ERROR_OK) {
        return res;
    }

    if (result == NULL) {
        return MCP356X_ERROR_FAILED;
    }

    *result = 2.0f * val * refVoltage * 5.96046483E-8 * (1 / gain_lookup(obj));

    return MCP356X_ERROR_OK;
}
