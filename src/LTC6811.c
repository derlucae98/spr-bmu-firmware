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

#include "LTC6811.h"

#define PAYLOADLEN 6

#define CFGR0_GPIO5     (1 << 7)
#define CFGR0_GPIO4     (1 << 6)
#define CFGR0_GPIO3     (1 << 5)
#define CFGR0_GPIO2     (1 << 4)
#define CFGR0_GPIO1     (1 << 3)
#define CFGR0_REFON     (1 << 2)
#define CFGR0_DTEN      (1 << 1)
#define CFGR0_ADCOPT    (1 << 0)

/*!
 * @enum Enumerator for the LTC6811 commands
 * @details Commands also contain the pre-computed 16 Bit PEC
 */
typedef enum {
    WRCFGA            = 0x00013D6E, //!< WRCFGA
    RDCFGA            = 0x00022B0A, //!< RDCFGA
    RDCVA             = 0x000407C2, //!< RDCVA
    RDCVB             = 0x00069A94, //!< RDCVB
    RDCVC             = 0x00085E52, //!< RDCVC
    RDCVD             = 0x000AC304, //!< RDCVD
    RDAUXA            = 0x000CEFCC, //!< RDAUXA
    RDAUXB            = 0x000E729A, //!< RDAUXB
    RDSTATA           = 0x0010ED72, //!< RDSTATA
    RDSTATB           = 0x00127024, //!< RDSTATB
    WRSCTRL           = 0x00145CEC, //!< WRSCTRL
    WRPWM             = 0x00200000, //!< WRPWM
    RDSCTRL           = 0x0016C1BA, //!< RDSCTRL
    RDPWM             = 0x00229D56, //!< RDPWM
    STSCTRL           = 0x00198E4E, //!< STSCTRL
    CLRSCTRL          = 0x0018057C, //!< CLRSCTRL
    ADCV_NORM_ALL     = 0x0360F46C, //!< ADCV_NORM_ALL
    ADOW_NORM_PUP_ALL = 0x03681C62, //!< ADOW_NORM_PUP_ALL
    ADOW_NORM_PDN_ALL = 0x0328FBE8, //!< ADOW_NORM_PDN_ALL
    CVST_NORM_ST1     = 0x0327B41C, //!< CVST_NORM_ST1
    ADOL_NORM         = 0x03012E88, //!< ADOL_NORM
    ADAX_NORM_GPIO1   = 0x05615892, //!< ADAX_NORM_GPIO1
    ADAX_NORM_GPIO2   = 0x05624EF6, //!< ADAX_NORM_GPIO2
    ADAXD_NORM_GPIO1  = 0x05010944, //!< ADAXD_NORM_GPIO1
    ADAXD_NORM_GPIO2  = 0x05021F20, //!< ADAXD_NORM_GPIO2
    ADAXD_NORM_2NDREF = 0x0506AEBE, //!< ADAXD_NORM_2NDREF
    ADAXD_NORM_ALL    = 0x05008276, //!< ADAXD_NORM_ALL
    ADAXD_FAST_ALL    = 0x04804E1C, //!< ADAXD_FAST_ALL
    AXST_NORM_ST1     = 0x052793D0, //!< AXST_NORM_ST1
    ADSTAT_NORM_ALL   = 0x05683BAE, //!< ADSTAT_NORM_ALL
    ADSTATD_NORM_ALL  = 0x05086A78, //!< ADSTATD_NORM_ALL
    STATST_NORM_ST1   = 0x052F7BDE, //!< STATST_NORM_ST1
    ADCVAX_NORM       = 0x056F9C54, //!< ADCVAX_NORM
    ADCVSC_NORM       = 0x0567745A, //!< ADCVSC_NORM
    CLRCELL           = 0x0711C9C0, //!< CLRCELL
    CLRAUX            = 0x0712DFA4, //!< CLRAUX
    CLRSTAT           = 0x07135496, //!< CLRSTAT
    PLADC             = 0x0714F36C, //!< PLADC
    DIAGN             = 0x0715785E, //!< DIAGN
    WRCOMM            = 0x072124B2, //!< WRCOMM
    RDCOMM            = 0x072232D6, //!< RDCOMM
    STCOMM            = 0x0723B9E4  //!< STCOMM
} commandLTC_t;

/*!
 * @enum Enumerator for the selectable GPIO pin on the LTC6811.
 * @see getGPIOVoltage()
 */
typedef enum {
    GPIO_1,//!< GPIO_1
    GPIO_2 //!< GPIO_2
} LTCGPIO_t;


static uint8_t prvConfig;

static ltc_spi_move_array_t prv_ltc_spi_move_array;
static ltc_assert_cs_t      prv_ltc_assert_cs;
static ltc_deassert_cs_t    prv_ltc_deassert_cs;
static ltc_mutex_take_t     prv_ltc_mutex_take;
static ltc_mutex_give_t     prv_ltc_mutex_give;

static void send_command(commandLTC_t command);
static void broadcast(commandLTC_t command);
static void broadcast_transmit(commandLTC_t command, uint8_t sendPayload[][PAYLOADLEN]);
static void broadcast_receive(commandLTC_t command, uint8_t recPayload[][PAYLOADLEN], LTCError_t *status);
static void broadcast_stcomm(void);
static inline uint16_t calc_pec(uint8_t len, const uint8_t *data);
static void check_pec(uint8_t payload[][8], LTCError_t *result);
static void set_config(uint8_t CFGR0, uint8_t gates[][MAX_NUM_OF_CELLS]);
static void read_cell_voltage_registers_and_convert_to_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], LTCError_t voltageStatus[][MAX_NUM_OF_CELLS]);
static void set_mux_channel(uint8_t temperature_channel);
static inline uint8_t return_mux_command_byte(uint8_t channel);
static void get_gpio_voltage(uint16_t voltGPIO[][2], LTCError_t *voltGPIOStatus);
static uint16_t calc_temperature_from_voltage(uint16_t B, uint16_t temp_voltage);


/*!
 * @enum Enumerator for the LTC8611 I2C function
 */
enum {
    I2C_START            = 0x60, //!< I2C_START
    I2C_STOP             = 0x10, //!< I2C_STOP
    I2C_BLANK            = 0x00, //!< I2C_BLANK
    I2C_NO_TRANSMIT      = 0x70, //!< I2C_NO_TRANSMIT
    I2C_MASTER_ACK       = 0x00, //!< I2C_MASTER_ACK
    I2C_MASTER_NACK      = 0x08, //!< I2C_MASTER_NACK
    I2C_MASTER_NACK_STOP = 0x09  //!< I2C_MASTER_NACK_STOP
};

//Just for convenience
#define I2C_HIGHBYTE(x) (x >> 4)
#define I2C_LOWBYTE(x) (x << 4)

//############################################################################################

void ltc6811_init(ltc_mutex_take_t ltc_mutex_take, ltc_mutex_give_t ltc_mutex_give, ltc_spi_move_array_t ltc_spi_move_array, ltc_assert_cs_t ltc_assert_cs, ltc_deassert_cs_t ltc_deassert_cs) {
    prvConfig = CFGR0_GPIO5 | CFGR0_GPIO4 | CFGR0_GPIO3 | CFGR0_GPIO2 | CFGR0_GPIO1 | CFGR0_REFON | CFGR0_ADCOPT;

    prv_ltc_mutex_take      = ltc_mutex_take;
    prv_ltc_mutex_give      = ltc_mutex_give;
    prv_ltc_spi_move_array  = ltc_spi_move_array;
    prv_ltc_assert_cs       = ltc_assert_cs;
    prv_ltc_deassert_cs     = ltc_deassert_cs;

    if (prv_ltc_mutex_take(portMAX_DELAY)) {
        ltc6811_wake_daisy_chain();
        set_config(prvConfig, NULL);
        prv_ltc_mutex_give();
    }

}

static void send_command(commandLTC_t command) {
    uint8_t command_temp[4];
    command_temp[0] = (uint8_t)  (command >> 24);
    command_temp[1] = (uint8_t) ((command & 0x00FF0000) >> 16);
    command_temp[2] = (uint8_t) ((command & 0x0000FF00) >> 8);
    command_temp[3] = (uint8_t)  (command);
    prv_ltc_spi_move_array(command_temp, 4);
}

void broadcast(commandLTC_t command) {
    prv_ltc_assert_cs();
    send_command(command);
    prv_ltc_deassert_cs();
}

static void broadcast_transmit(commandLTC_t command, uint8_t sendPayload[][PAYLOADLEN]) {
    uint16_t PEC[NUMBEROFSLAVES]; //PEC for the payload. Each payload has different PECs
    uint8_t send;

    for (size_t slave = NUMBEROFSLAVES; slave-- > 0; ) { //Reverse order since first byte will be shifted to the last slave in the chain!
        PEC[slave] = calc_pec(6, &sendPayload[slave][0]); //Calculate the payload PEC beforehand for every slave
    }

    prv_ltc_assert_cs();
    send_command(command);

    for (size_t slave = NUMBEROFSLAVES; slave-- > 0; ) { //Reverse order since first byte will be shifted to the last slave in the chain!
        for (size_t byte = 0; byte < 6; byte++) {
            prv_ltc_spi_move_array(&sendPayload[slave][byte], 1);
        }
        send = PEC[slave] >> 8;
        prv_ltc_spi_move_array(&send, 1);
        send = PEC[slave] & 0x00FF;
        prv_ltc_spi_move_array(&send, 1);
    }
    prv_ltc_deassert_cs();
}

static void broadcast_receive(commandLTC_t command, uint8_t recPayload[][PAYLOADLEN], uint8_t *status) {
    uint8_t tempRecPayload[NUMBEROFSLAVES][8]; //Holds payload and received PEC. PEC will be removed

    prv_ltc_assert_cs();
    send_command(command);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        memset(&tempRecPayload[slave][0], 0xFF, 8);
        prv_ltc_spi_move_array(&tempRecPayload[slave][0], 8);
    }
    prv_ltc_deassert_cs();
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        memcpy(&recPayload[slave][0], &tempRecPayload[slave][0], 6); //Copy only the first 6 bytes per payload
    }
    check_pec(tempRecPayload, status);
}

static void broadcast_stcomm(void) {
    prv_ltc_assert_cs();
    send_command(STCOMM);
    uint8_t send = 0xFF;
    for (size_t i = 0; i < 9; i++) {
        prv_ltc_spi_move_array(&send, 1); //72 clock cycles
    }
    prv_ltc_deassert_cs();
}

uint16_t calc_pec(uint8_t len, const uint8_t *data) {
    uint16_t PEC = 16;
    uint8_t IN[7];

    for (size_t byte = 0; byte < len; byte++) {
        for (size_t bit = 8; bit-- > 0;) {
            uint8_t DIN;
            DIN = (data[byte] >> bit) & 0x01;
            IN[0] = DIN   ^ ((PEC >> 14) & 0x01);
            IN[1] = IN[0] ^ ((PEC >>  2) & 0x01);
            IN[2] = IN[0] ^ ((PEC >>  3) & 0x01);
            IN[3] = IN[0] ^ ((PEC >>  6) & 0x01);
            IN[4] = IN[0] ^ ((PEC >>  7) & 0x01);
            IN[5] = IN[0] ^ ((PEC >>  9) & 0x01);
            IN[6] = IN[0] ^ ((PEC >> 13) & 0x01);

            PEC <<= 1;
            PEC = (PEC & ~(1 <<  0)) | IN[0];
            PEC = (PEC & ~(1 <<  3)) | IN[1] << 3;
            PEC = (PEC & ~(1 <<  4)) | IN[2] << 4;
            PEC = (PEC & ~(1 <<  7)) | IN[3] << 7;
            PEC = (PEC & ~(1 <<  8)) | IN[4] << 8;
            PEC = (PEC & ~(1 << 10)) | IN[5] << 10;
            PEC = (PEC & ~(1 << 14)) | IN[6] << 14;
        }
    }
    return PEC << 1;
}

void check_pec(uint8_t payload[][8], LTCError_t *result) {
    uint16_t PEC_should;
    uint16_t PEC_is;

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        PEC_is = calc_pec(6, &payload[slave][0]);
        PEC_should = (payload[slave][6] << 8) | payload[slave][7];
        if (PEC_should != PEC_is) {
            result[slave] = PECERROR;
        } else {
            result[slave] = NOERROR;
        }
    }
}

void ltc6811_wake_daisy_chain(void) {
    uint8_t send = 0xFF;
    for (size_t slave = 0; slave < NUMBEROFSLAVES + 1U; slave++) {
        prv_ltc_assert_cs();
        prv_ltc_spi_move_array(&send, 1);
        prv_ltc_deassert_cs();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void set_config(uint8_t CFGR0, uint8_t gates[][MAX_NUM_OF_CELLS]) {
    //Config register and balancing control use the same command.

    uint8_t payload[NUMBEROFSLAVES][PAYLOADLEN];
    memset(payload, 0, sizeof(payload));
    bool setBalancing = true;
    if (gates == NULL) {
        setBalancing = false;
    }

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++){
        payload[slave][0] = CFGR0;

        if (setBalancing) {
            for (size_t cell = 0; cell < 8; cell++) {
                payload[slave][4] |= ((gates[slave][cell] & 0x1) << cell);
            }
            payload[slave][5] = ((gates[slave][8] & 0x1) << 0) | ((gates[slave][9] & 0x1) << 1) |
                                ((gates[slave][10] & 0x1) << 2) | ((gates[slave][11] & 0x1) << 3);
        }
    }
    broadcast_transmit(WRCFGA, payload);
}

void ltc6811_get_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], LTCError_t voltageStatus[][MAX_NUM_OF_CELLS]) {
    //Start ADC conversion
    broadcast(ADCV_NORM_ALL);

    //Wait for conversion done
    vTaskDelay(pdMS_TO_TICKS(4));

    read_cell_voltage_registers_and_convert_to_voltage(voltage, voltageStatus);
}

void read_cell_voltage_registers_and_convert_to_voltage(uint16_t voltage[][MAX_NUM_OF_CELLS], LTCError_t voltageStatus[][MAX_NUM_OF_CELLS]) {
    uint8_t payload[NUMBEROFSLAVES][PAYLOADLEN];
    LTCError_t status[NUMBEROFSLAVES];

    //Load Cell Voltage Register A
    broadcast_receive(RDCVA, payload, status);
    //Set cell voltages 1 to 3
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        voltage[slave][0] = ((payload[slave][1] << 8) | payload[slave][0]);
        voltage[slave][1] = ((payload[slave][3] << 8) | payload[slave][2]);
        voltage[slave][2] = ((payload[slave][5] << 8) | payload[slave][4]);

        voltageStatus[slave][0] = status[slave];
        voltageStatus[slave][1] = status[slave];
        voltageStatus[slave][2] = status[slave];
    }

    //Load Cell Voltage Register B
    broadcast_receive(RDCVB, payload, status);
    //Set cell voltages 4 to 6
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        voltage[slave][3] = ((payload[slave][1] << 8) | payload[slave][0]);
        voltage[slave][4] = ((payload[slave][3] << 8) | payload[slave][2]);
        voltage[slave][5] = ((payload[slave][5] << 8) | payload[slave][4]);

        voltageStatus[slave][3] = status[slave];
        voltageStatus[slave][4] = status[slave];
        voltageStatus[slave][5] = status[slave];
    }

    //Load Cell Voltage Register C
    broadcast_receive(RDCVC, payload, status);
    //Set cell voltages 7 to 9
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        voltage[slave][6] = ((payload[slave][1] << 8) | payload[slave][0]);
        voltage[slave][7] = ((payload[slave][3] << 8) | payload[slave][2]);
        voltage[slave][8] = ((payload[slave][5] << 8) | payload[slave][4]);

        voltageStatus[slave][6] = status[slave];
        voltageStatus[slave][7] = status[slave];
        voltageStatus[slave][8] = status[slave];
    }

    //Load Cell Voltage Register D
    broadcast_receive(RDCVD, payload, status);
    //Set cell voltages 10 to 12
    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        voltage[slave][9] = ((payload[slave][1] << 8) | payload[slave][0]);
        voltage[slave][10] = ((payload[slave][3] << 8) | payload[slave][2]);
        voltage[slave][11] = ((payload[slave][5] << 8) | payload[slave][4]);

        voltageStatus[slave][9]  = status[slave];
        voltageStatus[slave][10] = status[slave];
        voltageStatus[slave][11] = status[slave];
    }
}

void set_mux_channel(uint8_t temperature_channel) {
    uint8_t cmd = 0;
    uint8_t payload[NUMBEROFSLAVES][PAYLOADLEN];

    cmd = return_mux_command_byte(temperature_channel);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        //Set mux
        payload[slave][0] = 0x69; //START, Control byte mux HIGH
        payload[slave][1] = 0x00; //Control byte mux LOW, ACK
        payload[slave][2] = 0x00 | ((cmd & 0xF0) >> 4); //BLANK, command HIGH
        payload[slave][3] = 0x00 | ((cmd & 0x0F) << 4); //command LOW, ACK
        payload[slave][4] = 0x10; //STOP
        payload[slave][5] = 0x09;
    }
    //Write control and command of mux
    broadcast_transmit(WRCOMM, payload);
    broadcast_stcomm();
}

inline uint8_t return_mux_command_byte(uint8_t channel) {
    uint8_t cmd_mux = 0;
    cmd_mux = (channel & 0x07) | (1 << 3);
    return cmd_mux;
}

void get_gpio_voltage(uint16_t voltGPIO[][2], LTCError_t *voltGPIOStatus) {
    uint8_t payload_auxa[NUMBEROFSLAVES][PAYLOADLEN];
    LTCError_t status[NUMBEROFSLAVES];
    broadcast(ADAXD_FAST_ALL);

    vTaskDelay(pdMS_TO_TICKS(2));

    broadcast_receive(RDAUXA, payload_auxa, status);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        voltGPIOStatus[slave] = status[slave];
        voltGPIO[slave][0] = (payload_auxa[slave][1] << 8) | payload_auxa[slave][0]; //GPIO1
        voltGPIO[slave][1] = (payload_auxa[slave][3] << 8) | payload_auxa[slave][2]; //GPIO2
    }
}

void ltc6811_get_temperatures_in_degC(uint16_t temperature[][MAX_NUM_OF_TEMPSENS], LTCError_t temperatureStatus[][MAX_NUM_OF_TEMPSENS], uint8_t startChannel, uint8_t len) {
    uint16_t voltGPIO[NUMBEROFSLAVES][2]; //GPIO1 and GPIO2 are converted simultaneously
    LTCError_t status[NUMBEROFSLAVES];

    for (size_t channel = startChannel; channel < startChannel + len; channel++) {
        set_mux_channel(channel);
        get_gpio_voltage(voltGPIO, status);

        for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
            temperatureStatus[slave][channel] = status[slave];
            temperatureStatus[slave][channel + 7] = status[slave];
            temperature[slave][channel] = calc_temperature_from_voltage(NTC_CELL, voltGPIO[slave][0]);
            temperature[slave][channel + 7] = calc_temperature_from_voltage(NTC_CELL, voltGPIO[slave][1]);
        }
    }
}

uint16_t calc_temperature_from_voltage(uint16_t B, uint16_t temp_voltage) {
    static const uint16_t R0 = 10000;
    static const float T0 = 273.15f;
    float recipT0 = 1 / (T0 + 25.0f);
    static const uint16_t Resistor = 18000;
    float R;
    float T;

    R = Resistor / ((30000 / (float)temp_voltage) - 1);
    T = (1 / ((log(R / R0) / B) + recipT0)) - T0;
    return (uint16_t) (T * 10);
}

void ltc6811_get_uid(uint32_t UID[]) {
    uint8_t payload[NUMBEROFSLAVES][PAYLOADLEN];
    uint32_t uid[NUMBEROFSLAVES];
    uint8_t  pec[NUMBEROFSLAVES];
    uint8_t  transmit[3];

    //Transmitted bytes are the same for all slaves
    transmit[0] = 0xA0; //Control byte write
    transmit[1] = 0xFC; //Address byte
    transmit[2] = 0xA1; //Control byte read

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        payload[slave][0] = I2C_START | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_ACK;
        payload[slave][2] = I2C_BLANK | I2C_HIGHBYTE(transmit[1]);
        payload[slave][3] = I2C_LOWBYTE(transmit[1]) | I2C_MASTER_ACK;
        payload[slave][4] = I2C_START | I2C_HIGHBYTE(transmit[2]);
        payload[slave][5]  = I2C_LOWBYTE(transmit[2]) | I2C_MASTER_ACK;
    }

    broadcast_transmit(WRCOMM, payload);
    broadcast_stcomm();

    transmit[0] = 0xFF; //dummy bytes for reading from slave
    transmit[1] = 0xFF; //dummy bytes for reading from slave
    transmit[2] = 0xFF; //dummy bytes for reading from slave

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        payload[slave][0] = I2C_BLANK | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_ACK;
        payload[slave][2] = I2C_BLANK | I2C_HIGHBYTE(transmit[1]);
        payload[slave][3] = I2C_LOWBYTE(transmit[1]) | I2C_MASTER_ACK;
        payload[slave][4] = I2C_BLANK | I2C_HIGHBYTE(transmit[2]);
        payload[slave][5] = I2C_LOWBYTE(transmit[2]) | I2C_MASTER_ACK;
    }

    broadcast_transmit(WRCOMM, payload);
    broadcast_stcomm();

    //Read COMM Register
    broadcast_receive(RDCOMM, payload, pec);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        if (pec[slave] != PECERROR) {
            uid[slave]  = ((payload[slave][0] & 0x0F) << 28) | ((payload[slave][1] & 0xF0) << 20);
            uid[slave] |= ((payload[slave][2] & 0x0F) << 20) | ((payload[slave][3] & 0xF0) << 12);
            uid[slave] |= ((payload[slave][4] & 0x0F) << 12) | ((payload[slave][5] & 0xF0) << 4);
        } else {
            uid[slave] = PECERROR;
        }
    }

    transmit[0] = 0xFF;
    transmit[1] = 0x00;
    transmit[2] = 0x00;

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        payload[slave][0] = I2C_BLANK | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_NACK_STOP;
        payload[slave][2] = I2C_NO_TRANSMIT;
        payload[slave][3] = 0x00;
        payload[slave][4] = I2C_NO_TRANSMIT;
        payload[slave][5] = 0x00;
    }

    //Load last Byte of UID in COMM Register and stop transaction
    broadcast_transmit(WRCOMM, payload);
    broadcast_stcomm();

    //Read COMM Register
    broadcast_receive(RDCOMM, payload, pec);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        if (pec[slave] != PECERROR && uid[slave] != PECERROR) {
            uid[slave] |= ((payload[slave][0] & 0x0F) << 4) | ((payload[slave][1] & 0xF0) >> 4);
        } else {
            uid[slave] = PECERROR;
        }
    }

    memcpy(UID, uid, sizeof(uint32_t) * NUMBEROFSLAVES);
}

void ltc6811_open_wire_check(LTCError_t result[][MAX_NUM_OF_CELLS+1]) {
    uint16_t voltage_pup[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
    uint8_t pec_pup[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
    uint16_t voltage_pdn[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
    uint8_t pec_pdn[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];
    int32_t volt_diff[NUMBEROFSLAVES][MAX_NUM_OF_CELLS];

    //Algorithm described in LTC6811 datasheet

    broadcast(ADOW_NORM_PUP_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PUP_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PUP_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));

    read_cell_voltage_registers_and_convert_to_voltage(voltage_pup, pec_pup);

    broadcast(ADOW_NORM_PDN_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PDN_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PDN_ALL);
    vTaskDelay(pdMS_TO_TICKS(3));

    read_cell_voltage_registers_and_convert_to_voltage(voltage_pdn, pec_pdn);

    for (size_t slave = 0; slave < NUMBEROFSLAVES; slave++) {
        for (size_t cell = 1; cell < MAX_NUM_OF_CELLS; cell++) {
            volt_diff[slave][cell] = voltage_pup[slave][cell]
                                            - voltage_pdn[slave][cell];
        }

        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS - 1; cell++) {
            if (volt_diff[slave][cell + 1] + 4000 < 0) {
                result[slave][cell + 1] = OPENCELLWIRE;
            } else {
                result[slave][cell + 1] = NOERROR;
            }
        }

        if (voltage_pup[slave][0] == 0) {
            result[slave][0] = OPENCELLWIRE;
        } else {
            result[slave][0] = NOERROR;
        }

        if (voltage_pdn[slave][11] == 0) {
            result[slave][12] = OPENCELLWIRE;
        } else {
            result[slave][12] = NOERROR;
        }
    }
}

void ltc6811_set_balancing_gates(uint8_t gates[][MAX_NUM_OF_CELLS]) {
    set_config(prvConfig, gates);
}
