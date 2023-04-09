/*!
 * @file            mcp356x.h
 * @brief           Library to interface with MCP356x(R) ADCs.
 *                  It is fully hardware independent and uses callback functions
 *                  to interface with the hardware. This library can be used with multiple
 *                  ADCs which are addressed by objects of type mcp356x_obj_t.
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

#ifndef MCP356X_H_
#define MCP356X_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/*! @typedef mcp356x_spi_move_array_t
 *  @brief Function pointer declaration for the SPI transceive function.
 *  @param a Pointer to the array to send. It will hold the received data afterwards.
 *  @param len Length of the array to send.
 */
typedef void (*mcp356x_spi_move_array_t)(uint8_t *a, size_t len);

/*! @typedef mcp356x_assert_cs_t
 *  @brief Function pointer declaration for the cs assert function.
 */
typedef void (*mcp356x_assert_cs_t)(void);

/*! @typedef mcp356x_deassert_cs_t
 *  @brief Function pointer declaration for the cs de-assert function.
 */
typedef void (*mcp356x_deassert_cs_t)(void);

/*!
 * @struct mcp356x_config_t
 * Data type for the configuration of the MCP356x ADC
 */
typedef struct {
    uint8_t VREF_SEL    : 1; //!< Reference selection. Only applicable for the MCP356xR devices with internal reference. @see ::VREF_SEL
    uint8_t CLK_SEL     : 2; //!< Clock selection. @see ::CLK_SEL
    uint8_t CS_SEL      : 2; //!< Sensor bias current source/sink selection. @see ::CS_SEL
    uint8_t ADC_MODE    : 2; //!< ADC operation mode. @see ::ADC_MODE
    uint8_t PRE         : 2; //!< Prescaler value selection for AMCLK. @see ::PRE
    uint8_t OSR         : 4; //!< Oversampling ratio for the Delta-Sigma A/D conversion. @see ::OSR
    uint8_t BOOST       : 2; //!< ADC bias current selection. @see ::BOOST
    uint8_t GAIN        : 3; //!< ADC gain selection. @see ::GAIN
    uint8_t AZ_MUX      : 1; //!< Auto-zeroing MUX setting. @see ::AZ_MUX
    uint8_t AZ_REF      : 1; //!< Auto-zeroing reference buffer setting. @see ::AZ_REF
    uint8_t CONV_MODE   : 2; //!< Conversion mode selection. @see ::CONV_MODE
    uint8_t DATA_FORMAT : 2; //!< ADC output data format selection. @see ::DATA_FORMAT
    uint8_t CRC_FORMAT  : 1; //!< CRC checksum format selection on read communications. @see ::CRC_FORMAT
    uint8_t EN_CRCCOM   : 1; //!< CRC checksum selection on read communications. @see ::EN_CRCCOM
    uint8_t EN_OFFCAL   : 1; //!< Enable digital offset calibration. NOT IMPLEMENTED!
    uint8_t EN_GAINCAL  : 1; //!< Enable digital gain calibration. NOT IMPLEMENTED!
    uint8_t IRQ_MODE    : 1; //!< Configuration for the IRQ/MDAT pin. @see ::IRQ_MODE
    uint8_t EN_FASTCMD  : 1; //!< Enable fast commands in the command byte. @see ::EN_FASTCMD
    uint8_t EN_STP      : 1; //!< Enable conversion start Interrupt output. @see ::EN_STP
    uint16_t SCAN;           //!< SCAN channel selection. @see mcp356x_scan_t
    uint8_t DLY;             //!< Time delay between each conversion durin scan cycle. @see ::DLY
    uint32_t TIMER;          //!< Selection bits for timer interval between two consecutive SCAN cycles.
} mcp356x_config_t;


typedef struct {
    mcp356x_spi_move_array_t spi_move_array;
    mcp356x_assert_cs_t assert_cs;
    mcp356x_deassert_cs_t deassert_cs;
    mcp356x_config_t config;
} mcp356x_obj_t;

/*!
 *  @enum VREF_SEL
 *  Internal Voltage Reference Bit
 */
enum VREF_SEL {
    VREF_SEL_EXT, //!< External reference
    VREF_SEL_INT  //!< Internal reference (Use this only for the MCP356x ADC without internal reference)
};

/*!
 *  @enum CLK_SEL
 *  Clock Selection
 */
enum CLK_SEL {
    CLK_SEL_EXT = 0,      //!< External digital clock (default)
    CLK_SEL_INT = 2,      //!< Internal clock is selected and no clock output is present on the CLK pin.
    CLK_SEL_INT_AMCLK_OUT //!< Internal clock is selected and AMCLK is present on the analog master clock output pin.
};

/*!
 *  @enum CS_SEL
 *  Current Source/Sink Selection Bits for Sensor Bias (source on V IN+/Sink on V IN-)
 */
enum CS_SEL {
    CS_SEL_OFF, //!< No current source is applied to the ADC inputs (default)
    CS_SEL_09,  //!< 0.9 uA is applied to the ADC inputs
    CS_SEL_37,  //!< 3.7 uA is applied to the ADC inputs
    CS_SEL_15   //!< 15 uA is applied to the ADC inputs
};

/*!
 *  @enum ADC_MODE
 *  ADC Operating Mode Selection
 */
enum ADC_MODE {
    ADC_MODE_SDN = 0, //!< ADC Shutdown mode (default)
    ADC_MODE_STB = 2, //!< ADC Standby mode
    ADC_MODE_CONV     //!< ADC Conversion mode
};

/*!
 *  @enum PRE
 *  Prescaler Value Selection for AMCLK
 */
enum PRE {
    PRE_1, //!< AMCLK = MCLK (default)
    PRE_2, //!< AMCLK = MCLK/2
    PRE_4, //!< AMCLK = MCLK/4
    PRE_8  //!< AMCLK = MCLK/8
};

/*!
 *  @enum OSR
 *  Oversampling Ratio for Delta-Sigma A/D Conversion
 */
enum OSR {
    OSR_32,    //!< OSR = 32
    OSR_64,    //!< OSR = 64
    OSR_128,   //!< OSR = 128
    OSR_256,   //!< OSR = 256 (default)
    OSR_512,   //!< OSR = 512
    OSR_1024,  //!< OSR = 1024
    OSR_2048,  //!< OSR = 2048
    OSR_4096,  //!< OSR = 4096
    OSR_8192,  //!< OSR = 8192
    OSR_16384, //!< OSR = 16384
    OSR_20480, //!< OSR = 20480
    OSR_24576, //!< OSR = 24576
    OSR_40960, //!< OSR = 40960
    OSR_49152, //!< OSR = 49152
    OSR_81920, //!< OSR = 81920
    OSR_98304  //!< OSR = 98304
};

/*!
 *  @enum BOOST
 *  ADC Bias Current Selection
 */
enum BOOST {
    BOOST_05,  //!< ADC channel has current x 0.5
    BOOST_066, //!< ADC channel has current x 0.66
    BOOST_1,   //!< ADC channel has current x 1 (default)
    BOOST_2    //!< ADC channel has current x 2
};

/*!
 *  @enum GAIN
 *  ADC Gain Selection
 */
enum GAIN {
    GAIN_033, //!< Gain is x1/3
    GAIN_1,   //!< Gain is x1 (default)
    GAIN_2,   //!< Gain is x2
    GAIN_4,   //!< Gain is x4
    GAIN_8,   //!< Gain is x8
    GAIN_16,  //!< Gain is x16
    GAIN_32,  //!< Gain is x32 (x16 analog, x2 digital)
    GAIN_64   //!< Gain is x64 (x16 analog, x4 digital)
};

/*!
 *  @enum AZ_MUX
 *  Auto-Zeroing MUX Setting
 */
enum AZ_MUX {
    AZ_MUX_DISABLED, //!< Analog input multiplexer auto-zeroing algorithm is disabled (default).
    AZ_MUX_ENABLED   /*!< DC auto-zeroing algorithm is enabled. This setting multiplies the conversion time by two and
                          does not allow Continuous Conversion mode operation (which is then replaced by a series of
                          consecutive One-Shot mode conversions).*/
};

/*!
 *  @enum AZ_REF
 *  Auto-Zeroing Reference Buffer Setting
 */
enum AZ_REF {
    AZ_REF_ENABLED, /*!< Internal voltage reference buffer chopping algorithm is enabled. This setting has no effect
                          when external voltage reference is selected (VREF_SEL = 0) (default).*/
    AZ_REF_DISABLED   //!< Internal voltage reference buffer chopping auto-zeroing algorithm is disabled.
};

/*!
 *  @enum CONV_MODE
 *  Conversion Mode Selection
 */
enum CONV_MODE {
    CONV_MODE_ONE_SHOT_SCAN_SDN = 0, /*!< One-shot conversion or one-shot cycle in SCAN mode. It sets ADC_MODE[1:0] to ‘0x’ (ADC
                                          Shutdown) at the end of the conversion or at the end of the conversion cycle in SCAN mode (default) */
    CONV_MODE_ONE_SHOT_SCAN_STB = 2, /*!< One-shot conversion or one-shot cycle in SCAN mode. It sets ADC_MODE[1:0] to ‘10’ (standby) at
                                          the end of the conversion or at the end of the conversion cycle in SCAN mode. */
    CONV_MODE_CONT_SCAN              /*!< Continuous Conversion mode or continuous conversion cycle in SCAN mode */
};

/*!
 *  @enum DATA_FORMAT
 *  ADC Output Data Format Selection
 */
enum DATA_FORMAT {
    DATA_FORMAT_24,         /*!< 24-bit (default ADC coding): 24-bit ADC data. It does not allow overrange (ADC code locked to
                                0xFFFFFF or 0x800000). */
    DATA_FORMAT_32,         /*!< 32-bit (24-bit left justified data): 24-bit ADC data + 0x00 (8-bit). It does not allow overrange (ADC
                                code locked to 0xFFFFFF or 0x800000). */
    DATA_FORMAT_32_SGN,     /*!< 32-bit (25-bit right justified data): SGN extension (8-bit) + 24-bit ADC data. It allows overrange with
                                the SGN extension */
    DATA_FORMAT_32_SGN_CHID /*!< 32-bit (25-bit right justified data + Channel ID): CHID[3:0] + SGN extension (4 bits) + 24-bit ADC
                                data. It allows overrange with the SGN extension. */
};

/*!
 *  @enum CRC_FORMAT
 *  CRC Checksum Format Selection on Read Communications
 */
enum CRC_FORMAT {
    CRC_FORMAT_16, //!< 16-bit wide (CRC-16 only) (default).
    CRC_FORMAT_32  //!< 32-bit wide (CRC-16 followed by 16 zeros)
};

/*!
 *  @enum EN_CRCCOM
 *  CRC Checksum Selection on Read Communications
 */
enum EN_CRCCOM {
    EN_CRCCOM_DISABLED, //!< CRC on communications disabled (default).
    EN_CRCCOM_ENABLED   //!< CRC on communications enabled.
};

/*!
 *  @enum IRQ_MODE
 *  Configuration for the IRQ/MDAT Pin
 */
enum IRQ_MODE {
    IRQ_MODE_IRQ_HIGH_Z,  /*!< IRQ output is selected. All interrupts can appear on the IRQ/MDAT pin. The Inactive state is high-Z (requires a pull-up resistor to DVDD) (default). */
    IRQ_MODE_IRQ_HIGH,    /*!< IRQ output is selected. All interrupts can appear on the IRQ/MDAT pin. The Inactive state is logic high (does not require a pull-up resistor to DVDD ). */
    IRQ_MODE_MDAT_HIGH_Z, /*!< MDAT output is selected. Only POR and CRC interrupts can be present on this pin and take priority
                               over the MDAT output. The Inactive state is high-Z (requires a pull-up resistor to DVDD) (default).*/
    IRQ_MODE_MDAT_HIGH    /*!< MDAT output is selected. Only POR and CRC interrupts can be present on this pin and take priority
                               over the MDAT output. The Inactive state is logic high (does not require a pull-up resistor to DVDD).*/
};

/*!
 *  @enum EN_FASTCMD
 *  Enable Fast Commands in the COMMAND Byte
 */
enum EN_FASTCMD {
    EN_FASTCMD_ENABLED, //!< Fast commands are enabled (default).
    EN_FASTCMD_DISABLED //!< Fast commands are disabled.
};

/*!
 *  @enum EN_STP
 *  Enable Conversion Start Interrupt Output
 */
enum EN_STP {
    EN_STP_ENABLED, //!< Conversion Start Interrupt Output enabled (default)
    EN_STP_DISABLED //!< Conversion Start Interrupt Output disabled
};

/*!
 *  @enum mcp356x_channel_t
 *  MUX_VIN+/- Input Selection
 */
typedef enum {
    MUX_CH0,            //!< Channel 0 (default)
    MUX_CH1,            //!< Channel 1
    MUX_CH2,            //!< Channel 2
    MUX_CH3,            //!< Channel 3
    MUX_CH4,            //!< Channel 4
    MUX_CH5,            //!< Channel 5
    MUX_CH6,            //!< Channel 6
    MUX_CH7,            //!< Channel 7
    MUX_AGND,           //!< Channel AGND
    MUX_AVDD,           //!< Channel AVDD
    MUX_RESERVED,       //!< Channel Reserved (do not use)
    MUX_REFIN_POS,      //!< Channel REFIN+/OUT
    MUX_REFIN_NEG,      //!< Channel REFIN-
    MUX_INT_TEMP_P,     //!< Channel Internal Temperature Sensor Diode P (Temp Diode P)
    MUX_INT_TEMP_M,     //!< Channel Internal Temperature Sensor Diode M (Temp Diode M)
    MUX_INT_VCM         //!< Channel Internal VCM
} mcp356x_channel_t;

/*!
 *  @enum mcp356x_scan_t
 *  SCAN Channel Selection
 */
typedef enum {
    SCAN_OFFSET       = 0x8000, //!< Channel: OFFSET
    SCAN_VCM          = 0x4000, //!< Channel: VCM
    SCAN_AVDD         = 0x2000, //!< Channel: AVDD
    SCAN_TEMP         = 0x1000, //!< Channel: TEMP
    SCAN_DIFF_CH6_CH7 = 0x0800, //!< Channel: Differential Channel D (CH6-CH7)
    SCAN_DIFF_CH4_CH5 = 0x0400, //!< Channel: Differential Channel C (CH4-CH5)
    SCAN_DIFF_CH2_CH3 = 0x0200, //!< Channel: Differential Channel B (CH2-CH3)
    SCAN_DIFF_CH0_CH1 = 0x0100, //!< Channel: Differential Channel A (CH0-CH1)
    SCAN_SE_CH7       = 0x0080, //!< Channel: Single-Ended Channel CH7
    SCAN_SE_CH6       = 0x0040, //!< Channel: Single-Ended Channel CH6
    SCAN_SE_CH5       = 0x0020, //!< Channel: Single-Ended Channel CH5
    SCAN_SE_CH4       = 0x0010, //!< Channel: Single-Ended Channel CH4
    SCAN_SE_CH3       = 0x0008, //!< Channel: Single-Ended Channel CH3
    SCAN_SE_CH2       = 0x0004, //!< Channel: Single-Ended Channel CH2
    SCAN_SE_CH1       = 0x0002, //!< Channel: Single-Ended Channel CH1
    SCAN_SE_CH0       = 0x0001  //!< Channel: Single-Ended Channel CH0
} mcp356x_scan_t;

/*!
 *  @enum DLY
 *  Delay Time (T_DLY_SCAN) Between Each Conversion During a SCAN Cycle
 */
enum DLY {
    SCAN_DLY_512    = 7, //!< 512 * DMCLK
    SCAN_DLY_256    = 6, //!< 256 * DMCLK
    SCAN_DLY_128    = 5, //!< 128 * DMCLK
    SCAN_DLY_64     = 4, //!< 64 * DMCLK
    SCAN_DLY_32     = 3, //!< 32 * DMCLK
    SCAN_DLY_16     = 2, //!< 16 * DMCLK
    SCAN_DLY_8      = 1, //!< 8 * DMCLK
    SCAN_DLY_NO_DLY = 0  //!< No delay (default)
};

/*!
 *  @enum mcp356x_error_t
 *  Return type for the mcp356x_ functions.
 */
typedef enum {
    MCP356X_ERROR_OK,     //!< No error
    MCP356X_ERROR_FAILED, //!< Communication error
    MCP356X_ERROR_CRC     //!< CRC error
} mcp356x_error_t;

/*!
 *  @brief Initializes the ADC object with the callback functions.
 *  @param mcp356x_spi_move_array Callback for the SPI transceive function. @see mcp356x_spi_move_array_t for parameters.
 *  @param mcp356x_assert_cs Callback for the chip-select assert function. @see mcp356x_assert_cs_t for parameters.
 *  @param mcp356x_deassert_cs Callback for the chip-select de-assert function. @see mcp356x_deassert_cs_t for parameters.
 *  @return Returns an object which can be passed to any other function.
 *  @code
 *  //Example:
 *  mcp356x_obj_t adc;
 *  adc = mcp356x_init(adc_spi, adc_assert, adc_deassert);
 *  @endcode
 */
mcp356x_obj_t mcp356x_init(mcp356x_spi_move_array_t mcp356x_spi_move_array,
                           mcp356x_assert_cs_t mcp356x_assert_cs,
                           mcp356x_deassert_cs_t mcp356x_deassert_cs);

/*!
 *  @brief Resets the ADC.
 *  @param obj Object of the ADC of choice.
 *  @return Returns MCP356X_ERROR_OK on success and MCP356X_ERROR_FAILED on failure. @see mcp356x_error_t
 */
mcp356x_error_t mcp356x_reset(mcp356x_obj_t *obj);

/*!
 *  @brief Configure the ADC.
 *  Requires an initialized mcp356x_obj_t object.
 *  @param obj Object of the ADC of choice.
 *  @return Returns MCP356X_ERROR_OK on success and MCP356X_ERROR_FAILED on failure. @see mcp356x_error_t
 *  @code
 *  //Example:
 *  adc.config.VREF_SEL = VREF_SEL_EXT;
 *  adc.config.prvAdc.config.CLK_SEL = CLK_SEL_INT;
 *  //...
 *  mcp356x_set_config(&adc);
 *  @endcode
 */
mcp356x_error_t mcp356x_set_config(mcp356x_obj_t *obj);

/*!
 *  @brief Start a measurement.
 *  @param obj Object of the ADC of choice.
 *  @param ch_pos Positive channel. @see mcp356x_channel_t
 *  @param ch_neg Negative channel. @see mcp356x_channel_t
 *  @return Returns MCP356X_ERROR_OK on success and MCP356X_ERROR_FAILED on failure. @see mcp356x_error_t
 *  @note This is not needed in Scan mode.
 */
mcp356x_error_t mcp356x_acquire(mcp356x_obj_t *obj,
                                mcp356x_channel_t ch_pos,
                                mcp356x_channel_t ch_neg);

/*!
 *  @brief Start a measurement, wait for the ADC to be ready, and read the value.
 *  @param obj Object of the ADC of choice.
 *  @param ch_pos Positive channel. @see mcp356x_channel_t
 *  @param ch_neg Negative channel. @see mcp356x_channel_t
 *  @param val The ADC value will be stored here
 *  @param sgn Sign of the value. 0 = positive, 1 = negative. Set to NULL if not used.
 *  @param chID Channel ID. Used in conjunction with SCAN mode. Set to NULL if not used.
 *  @return Returns MCP356X_ERROR_OK on success and MCP356X_ERROR_FAILED on failure. @see mcp356x_error_t
 */
mcp356x_error_t mcp356x_get_value(mcp356x_obj_t *obj,
                                  mcp356x_channel_t ch_pos,
                                  mcp356x_channel_t ch_neg,
                                  int32_t *val,
                                  uint8_t *sgn,
                                  uint8_t *chID);

/*!
 *  @brief Poll the ADC and read the value after the ADC has completed the measurement.
 *  @param obj Object of the ADC of choice.
 *  @param val The ADC value will be stored here
 *  @param sgn Sign of the value. 0 = positive, 1 = negative. Set to NULL if not used.
 *  @param chID Channel ID. Used in conjunction with SCAN mode. Set to NULL if not used.
 *  @return Returns MCP356X_ERROR_OK on success and MCP356X_ERROR_FAILED on failure. @see mcp356x_error_t
 */
mcp356x_error_t mcp356x_read_value(mcp356x_obj_t *obj,
                                   int32_t *val,
                                   uint8_t *sgn,
                                   uint8_t *chID);

#endif /* MCP356X_H_ */
