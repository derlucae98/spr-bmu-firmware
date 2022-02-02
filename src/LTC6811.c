/*
Copyright 2020 Luca Engelmann

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

#define NTC_CELL 3960
#define NTC_PCB  3900


static uint32_t _UID[MAXSTACKS];
static uint8_t _numberOfSlaves = 0;
static uint16_t _cellOverVoltageThreshold;
static uint16_t _cellUnderVoltageThreshold;
static uint16_t _undervoltage_reg;
static uint16_t _overvoltage_reg;
battery_data_t batteryData;
static SemaphoreHandle_t _batteryDataMutex = NULL;
static uint8_t _workerTasksPrio;



//Helper functions, only used in this file!
//############################################################################################

/*!
 * @brief LTC6811 helper function
 * @details Pulls the /CS pin low
 */
static inline void select_bms(void);

/*!
 * @brief LTC6811 helper function
 * @details Releases the /CS pin
 */
static inline void deselect_bms(void);

/*!
 * @brief LTC6811 helper function
 * @details Send a command to all slaves.
 * @param command Command of type commandLTC_t
 * @param payload For commands which send or receive a payload, the payload can be specified.\n
 * Set to NULL if the commands does not have a payload.
 */
static void broadcast(commandLTC_t command, uint8_t sendPayload[][PAYLOADLEN], uint8_t recPayload[][PAYLOADLEN], uint8_t *recPEC);

/*!
 * @brief LTC6811 helper function
 * @details Calculates the PEC for a payload
 * @param len Number of bytes used to calculate the PEC
 * @param data Payload data for which the PEC is calculated
 * @return Two PEC bytes combined in a 16 bit integer, big endian
 */
static inline uint16_t calc_pec(uint8_t len, const uint8_t *data);

/*!
 * @brief LTC6811 helper function
 * @details Compares the transmitted PEC with the calculated PEC.
 * @param payload Data to check
 * @param result Stores the result for every payload blob (see enum LTCError_t for possible values)
 */
static void check_pec(uint8_t payload[][8], uint8_t *result);

/*!
 * @brief Wakes a daisy chain of _numberOfSlaves slaves
 */
static void ltc_wake_daisy_chain();

/*!
 * @brief LTC6811 helper function
 * @details Set the LTC configuration. The configuration is fixed.
 */
static void set_config(void);

/*!
 * @brief LTC6811 helper function
 * @details Performs the ADC conversion on all cells of all slaves and stores the result in an array
 * @param voltage Array in which the voltages will be stored.\n
 * An error code is stored in the lower byte if an error occurs
 */
static void get_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]);

/*!
 * @brief LTC6811 helper function
 * @details Reads the cell voltage registers after an ADC conversion and converts the register values to voltages
 * @param voltage Array in which the voltages will be stored.\n
 * An error code is stored in the lower byte if an error occurs @see LTCError_t
 * @see convertCellVoltageRegisterToVoltage(), getVoltage()
 */
static void read_cell_voltage_registers_and_convert_to_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]);

/*!
 * @brief LTC6811 helper function
 * @details Sets the multiplexer channel for the NTC conversion to a specific channel.
 * @param temperature_channel ADC channel. Both multiplexers are set to the same channel.
 */
static void set_mux_channel(uint8_t temperature_channel);

/*!
 * @brief LTC6811 helper function
 * @details Returns the command byte for the NTC multiplexers based on the channel.
 * @param channel Mux channel (0..6)
 * @return Command byte
 */
static inline uint8_t return_mux_command_byte(uint8_t channel);

/*!
 * @brief LTC6811 helper function
 * @details Performs an ADC conversion of a specific GPIO pin.
 * @param gpio GPIO pin of type LTCGPIO_t
 * @param voltGPIO Array in which the voltages will be stored.
 * @param channel Position in the voltGPIO array where the voltage will be placed
 */
static void get_gpio_voltage(uint16_t voltGPIO[][2], uint8_t *voltGPIOStatus);

/*!
 * @brief LTC6811 helper function
 * @details Selects a mux channel, performs the ADC conversion and returns the temperature\n
 * in degree celsius as fixed point integer value with one decimal place.
 * @param temperature Array in which the temperatures will be stored.
 */
static void get_temperatures_in_degC(uint16_t temperature[][MAXTEMPSENS], uint8_t temperatureStatus[][MAXTEMPSENS]);

/*!
 * @brief LTC6811 helper function
 * @details Calculates the NTC temperature based on the measured voltage
 * @param ntc NTC type. @see NTC_Type_t
 * @param temp_voltage Voltage which is converted to a temperature
 * @param ref_volt Measured reference voltage for each slave.
 * @return Temperature as fixed point integer value with one decimal place
 */
static uint16_t calc_temperature_from_voltage(uint16_t B, uint16_t temp_voltage);

/*!
 * @brief LTC6811 helper function
 * @details Gets the 32 bit unique ID stored in an EEPROM on the slave for every slave
 * @param UID Array in which the UIDs will be stored.\n
 * An error code is stored in the lower byte if an error occurs
 */
static void get_uid(uint32_t UID[]);

/*!
 * @brief LTC6811 helper function
 * @details Performs an open wire check on the all slaves.
 * @param result Array in which the results will be stored. See enum LTCError_t for possible values.
 */
static void open_wire_check(uint8_t result[][MAXCELLS+1]);

/*!
 *  @brief LTC6811 helper function
 *  @details Enables/disables the desired balancing gates on the slaves.
 *  @param gates Array for the values of all 12*12 gates (0 = off, else on)
 */
static void set_balancing_gates(uint8_t gates[][MAXCELLS]);

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

void ltc_init(LTC_initial_data_t initData) {
    _batteryDataMutex = xSemaphoreCreateMutex();
    configASSERT(_batteryDataMutex);
    memset(&batteryData, 0, sizeof(battery_data_t));
    _numberOfSlaves = initData.numberOfSlaves;
    _workerTasksPrio = initData.workerTasksPrio;
    _cellUnderVoltageThreshold = initData.undervoltageThreshold;
    _cellOverVoltageThreshold = initData.overvoltageThreshold;
    xTaskCreate(ltc_config_slaves_task, "LTC6811 conf", 300, NULL, 4, NULL);
}

// Helper functions

void select_bms(void) {
    clear_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
}

void deselect_bms(void) {
    set_pin(CS_SLAVES_PORT, CS_SLAVES_PIN);
}

void broadcast(commandLTC_t command, uint8_t sendPayload[][PAYLOADLEN], uint8_t recPayload[][PAYLOADLEN], uint8_t *recPEC) {
    uint8_t command_temp[4];
    uint16_t PEC[_numberOfSlaves]; //PEC for the payload. Each payload has different PECs
    uint8_t tempRecPayload[_numberOfSlaves][8]; //Holds payload and received PEC. PEC will be removed

    command_temp[0] = (uint8_t)  (command >> 24);
    command_temp[1] = (uint8_t) ((command & 0x00FF0000) >> 16);
    command_temp[2] = (uint8_t) ((command & 0x0000FF00) >> 8);
    command_temp[3] = (uint8_t)  (command);

    switch (command) {

    //Commands without transmitted or received payload
    case STSCTRL:
    case CLRSCTRL:
    case ADCV_NORM_ALL:
    case ADOW_NORM_PDN_ALL:
    case ADOW_NORM_PUP_ALL:
    case CVST_NORM_ST1:
    case ADOL_NORM:
    case ADAX_NORM_GPIO1:
    case ADAX_NORM_GPIO2:
    case ADAXD_NORM_GPIO1:
    case ADAXD_NORM_GPIO2:
    case ADAXD_NORM_2NDREF:
    case ADAXD_NORM_ALL:
    case AXST_NORM_ST1:
    case ADSTAT_NORM_ALL:
    case ADSTATD_NORM_ALL:
    case STATST_NORM_ST1:
    case ADCVAX_NORM:
    case ADCVSC_NORM:
    case CLRCELL:
    case CLRAUX:
    case CLRSTAT:
    case DIAGN:

        select_bms();
        spi_move_array(LTC6811_SPI, command_temp, 4);
        deselect_bms();
        break;

    //Commands with transmitted payload
    case WRCFGA:
    case WRSCTRL:
    case WRPWM:
    case WRCOMM:

        for (size_t slave = _numberOfSlaves; slave-- > 0; ) { //Reverse order since first byte will be shifted to the last slave in the chain!
            PEC[slave] = calc_pec(6, &sendPayload[slave][0]); //Calculate the payload PEC beforehand for every slave
        }

        select_bms();
        spi_move_array(LTC6811_SPI, command_temp, 4);

        for (size_t slave = _numberOfSlaves; slave-- > 0; ) { //Reverse order since first byte will be shifted to the last slave in the chain!
            for (size_t byte = 0; byte < 6; byte++) {
                spi_move(LTC6811_SPI, sendPayload[slave][byte]);
            }
            spi_move(LTC6811_SPI, (uint8_t)(PEC[slave] >> 8));
            spi_move(LTC6811_SPI, (uint8_t)(PEC[slave] & 0x00FF));
        }
        deselect_bms();
        break;

    //Commands with received payload
    case RDCFGA:
    case RDCVA:
    case RDCVB:
    case RDCVC:
    case RDCVD:
    case RDAUXA:
    case RDAUXB:
    case RDSTATA:
    case RDSTATB:
    case RDSCTRL:
    case RDPWM:
    case RDCOMM:

        select_bms();
        spi_move_array(LTC6811_SPI, command_temp, 4);

        for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
            memset(&tempRecPayload[slave][0], 0xFF, 8);
            spi_move_array(LTC6811_SPI, &tempRecPayload[slave][0], 8);
        }
        deselect_bms();
        for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
            memcpy(&recPayload[slave][0], &tempRecPayload[slave][0], 6); //Copy only the first 6 bytes per payload
        }
        check_pec(tempRecPayload, recPEC);
        break;

    //Command with 72 additional clock cycles
    case STCOMM:

        select_bms();
        spi_move_array(LTC6811_SPI, command_temp, 4);
        for (size_t i = 0; i < 9; i++) {
            spi_move(LTC6811_SPI, 0xFF); //72 clock cycles
        }
        deselect_bms();
        break;

    default:
        break;
    }
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

void check_pec(uint8_t payload[][8], uint8_t *result) {
    uint16_t PEC_should;
    uint16_t PEC_is;

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        PEC_is = calc_pec(6, &payload[slave][0]);
        PEC_should = (payload[slave][6] << 8) | payload[slave][7];
        if (PEC_should != PEC_is) {
            result[slave] = PECERROR;
        } else {
            result[slave] = NOERROR;
        }
    }
}

void ltc_wake_daisy_chain() {

    for (size_t slave = 0; slave < _numberOfSlaves + 1U; slave++) {
        select_bms();
        spi_move(LTC6811_SPI, 0xFF);
        deselect_bms();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

}

void set_config(void) {
    uint8_t payload[_numberOfSlaves][PAYLOADLEN];

    _undervoltage_reg = (_cellUnderVoltageThreshold / (16 * 100E-3)) - 1;
    _overvoltage_reg = _cellOverVoltageThreshold / (16 * 100E-3);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++){
        payload[slave][0] = 0xFC;
        payload[slave][1] = _undervoltage_reg & 0x00FF;
        payload[slave][2] = ((_undervoltage_reg & 0x0F00) >> 8)
                      | ((_overvoltage_reg & 0x000F) << 4);
        payload[slave][3] = ((_overvoltage_reg & 0x0FF0) >> 4);
        payload[slave][4] = 0x00;
        payload[slave][5] = 0x00;
    }
    broadcast(WRCFGA, payload, NULL, NULL);
}

void get_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]) {

    //Start ADC conversion
    broadcast(ADCV_NORM_ALL, NULL, NULL, NULL);

    //Wait for conversion done
    vTaskDelay(pdMS_TO_TICKS(3));

    read_cell_voltage_registers_and_convert_to_voltage(voltage, voltageStatus);
}

void read_cell_voltage_registers_and_convert_to_voltage(uint16_t voltage[][MAXCELLS], uint8_t voltageStatus[][MAXCELLS]) {
    uint8_t payload[_numberOfSlaves][PAYLOADLEN];
    uint8_t pec[_numberOfSlaves];

    //Load Cell Voltage Register A
    broadcast(RDCVA, NULL, payload, pec);
    //Set cell voltages 1 to 3
    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        voltage[slave][0] = ((payload[slave][1] << 8) | payload[slave][0]) / 10;
        voltage[slave][1] = ((payload[slave][3] << 8) | payload[slave][2]) / 10;
        voltage[slave][2] = ((payload[slave][5] << 8) | payload[slave][4]) / 10;

        voltageStatus[slave][0] = pec[slave];
        voltageStatus[slave][1] = pec[slave];
        voltageStatus[slave][2] = pec[slave];
    }

    //Load Cell Voltage Register B
    broadcast(RDCVB, NULL, payload, pec);
    //Set cell voltages 4 to 6
    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        voltage[slave][3] = ((payload[slave][1] << 8) | payload[slave][0]) / 10;
        voltage[slave][4] = ((payload[slave][3] << 8) | payload[slave][2]) / 10;
        voltage[slave][5] = ((payload[slave][5] << 8) | payload[slave][4]) / 10;

        voltageStatus[slave][3] = pec[slave];
        voltageStatus[slave][4] = pec[slave];
        voltageStatus[slave][5] = pec[slave];
    }

    //Load Cell Voltage Register C
    broadcast(RDCVC, NULL, payload, pec);
    //Set cell voltages 7 to 9
    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        voltage[slave][6] = ((payload[slave][1] << 8) | payload[slave][0]) / 10;
        voltage[slave][7] = ((payload[slave][3] << 8) | payload[slave][2]) / 10;
        voltage[slave][8] = ((payload[slave][5] << 8) | payload[slave][4]) / 10;

        voltageStatus[slave][6] = pec[slave];
        voltageStatus[slave][7] = pec[slave];
        voltageStatus[slave][8] = pec[slave];
    }

    //Load Cell Voltage Register D
    broadcast(RDCVD, NULL, payload, pec);
    //Set cell voltages 10 to 12
    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        voltage[slave][9] = ((payload[slave][1] << 8) | payload[slave][0]) / 10;
        voltage[slave][10] = ((payload[slave][3] << 8) | payload[slave][2]) / 10;
        voltage[slave][11] = ((payload[slave][5] << 8) | payload[slave][4]) / 10;

        voltageStatus[slave][9]  = pec[slave];
        voltageStatus[slave][10] = pec[slave];
        voltageStatus[slave][11] = pec[slave];
    }
}

void set_mux_channel(uint8_t temperature_channel) {
    uint8_t cmd = 0;
    uint8_t payload[_numberOfSlaves][PAYLOADLEN];

    cmd = return_mux_command_byte(temperature_channel);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        //Set mux
        payload[slave][0] = 0x69; //START, Control byte mux HIGH
        payload[slave][1] = 0x00; //Control byte mux LOW, ACK
        payload[slave][2] = 0x00 | ((cmd & 0xF0) >> 4); //BLANK, command HIGH
        payload[slave][3] = 0x00 | ((cmd & 0x0F) << 4); //command LOW, ACK
        payload[slave][4] = 0x10; //STOP
        payload[slave][5] = 0x09;
    }
    //Write control and command of mux
    broadcast(WRCOMM, payload, NULL, NULL);
    broadcast(STCOMM, NULL, NULL, NULL);
}

inline uint8_t return_mux_command_byte(uint8_t channel) {
    uint8_t cmd_mux = 0;
    cmd_mux = (channel & 0x07) | (1 << 3);
    return cmd_mux;
}

void get_gpio_voltage(uint16_t voltGPIO[][2], uint8_t *voltGPIOStatus) {
    uint8_t payload_auxa[_numberOfSlaves][PAYLOADLEN];
    uint8_t pec[_numberOfSlaves];
    broadcast(ADAXD_NORM_ALL, NULL, NULL, NULL);

    vTaskDelay(pdMS_TO_TICKS(3));

    broadcast(RDAUXA, NULL, payload_auxa, pec);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        voltGPIOStatus[slave] = pec[slave];
        voltGPIO[slave][0] = (payload_auxa[slave][1] << 8) | payload_auxa[slave][0]; //GPIO1
        voltGPIO[slave][1] = (payload_auxa[slave][3] << 8) | payload_auxa[slave][2]; //GPIO2
    }
}

void get_temperatures_in_degC(uint16_t temperature[][MAXTEMPSENS], uint8_t temperatureStatus[][MAXTEMPSENS]) {
    uint16_t voltGPIO[_numberOfSlaves][2]; //GPIO1 and GPIO2 are converted simultaneously
    uint8_t status[_numberOfSlaves];

    for (size_t channel = 0; channel < 7; channel++) {
        set_mux_channel(channel); //Select channels channel+1 and channel+9
        get_gpio_voltage(voltGPIO, status); //Get channel+1 and channel+9
        //PRINTF("%u\n", voltGPIO[2][1]);
        for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
            temperatureStatus[slave][channel] = status[slave];
            temperatureStatus[slave][channel+7] = status[slave];
            if (channel == 6) { //PCB Sensor
                temperature[slave][channel] = calc_temperature_from_voltage(NTC_PCB, voltGPIO[slave][0]);
                temperature[slave][channel + 7] = calc_temperature_from_voltage(NTC_PCB, voltGPIO[slave][1]);
            } else {
                temperature[slave][channel] = calc_temperature_from_voltage(NTC_CELL, voltGPIO[slave][0]);
                temperature[slave][channel + 7] = calc_temperature_from_voltage(NTC_CELL, voltGPIO[slave][1]);
            }
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

void get_uid(uint32_t UID[]) {
    uint8_t payload[_numberOfSlaves][PAYLOADLEN];
    uint32_t uid[_numberOfSlaves];
    uint8_t  pec[_numberOfSlaves];
    uint8_t  transmit[3];

    //Transmitted bytes are the same for all slaves
    transmit[0] = 0xA0; //Control byte write
    transmit[1] = 0xFC; //Address byte
    transmit[2] = 0xA1; //Control byte read

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        payload[slave][0] = I2C_START | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_ACK;
        payload[slave][2] = I2C_BLANK | I2C_HIGHBYTE(transmit[1]);
        payload[slave][3] = I2C_LOWBYTE(transmit[1]) | I2C_MASTER_ACK;
        payload[slave][4] = I2C_START | I2C_HIGHBYTE(transmit[2]);
        payload[slave][5]  = I2C_LOWBYTE(transmit[2]) | I2C_MASTER_ACK;
    }

    broadcast(WRCOMM, payload, NULL, NULL);
    broadcast(STCOMM, NULL, NULL, NULL);

    transmit[0] = 0xFF; //dummy bytes for reading from slave
    transmit[1] = 0xFF; //dummy bytes for reading from slave
    transmit[2] = 0xFF; //dummy bytes for reading from slave

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        payload[slave][0] = I2C_BLANK | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_ACK;
        payload[slave][2] = I2C_BLANK | I2C_HIGHBYTE(transmit[1]);
        payload[slave][3] = I2C_LOWBYTE(transmit[1]) | I2C_MASTER_ACK;
        payload[slave][4] = I2C_BLANK | I2C_HIGHBYTE(transmit[2]);
        payload[slave][5] = I2C_LOWBYTE(transmit[2]) | I2C_MASTER_ACK;
    }

    broadcast(WRCOMM, payload, NULL, NULL);
    broadcast(STCOMM, NULL, NULL, NULL);

    //Read COMM Register
    broadcast(RDCOMM, NULL, payload, pec);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
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

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        payload[slave][0] = I2C_BLANK | I2C_HIGHBYTE(transmit[0]);
        payload[slave][1] = I2C_LOWBYTE(transmit[0]) | I2C_MASTER_NACK_STOP;
        payload[slave][2] = I2C_NO_TRANSMIT;
        payload[slave][3] = 0x00;
        payload[slave][4] = I2C_NO_TRANSMIT;
        payload[slave][5] = 0x00;
    }

    //Load last Byte of UID in COMM Register and stop transaction
    broadcast(WRCOMM, payload, NULL, NULL);
    broadcast(STCOMM, NULL, NULL, NULL);

    //Read COMM Register
    broadcast(RDCOMM, NULL, payload, pec);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        if (pec[slave] != PECERROR && uid[slave] != PECERROR) {
            uid[slave] |= ((payload[slave][0] & 0x0F) << 4) | ((payload[slave][1] & 0xF0) >> 4);
        } else {
            uid[slave] = PECERROR;
        }
    }

    memcpy(UID, uid, sizeof(uint32_t) * _numberOfSlaves);
}

void open_wire_check(uint8_t result[][MAXCELLS+1]) {
    uint16_t voltage_pup[_numberOfSlaves][MAXCELLS];
    uint8_t pec_pup[_numberOfSlaves][MAXCELLS];
    uint16_t voltage_pdn[_numberOfSlaves][MAXCELLS];
    uint8_t pec_pdn[_numberOfSlaves][MAXCELLS];
    int32_t  volt_diff[_numberOfSlaves][MAXCELLS];


    //Algorithm described in LTC6811 datasheet

    broadcast(ADOW_NORM_PUP_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PUP_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PUP_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    read_cell_voltage_registers_and_convert_to_voltage(voltage_pup, pec_pup);

    broadcast(ADOW_NORM_PDN_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PDN_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    broadcast(ADOW_NORM_PDN_ALL, NULL, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(3));
    read_cell_voltage_registers_and_convert_to_voltage(voltage_pdn, pec_pdn);

    for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
        for (size_t cell = 1; cell < MAXCELLS; cell++) {
            volt_diff[slave][cell] = voltage_pup[slave][cell]
                                            - voltage_pdn[slave][cell];
        }

        for (size_t cell = 0; cell < MAXCELLS - 1; cell++) {
            if (volt_diff[slave][cell + 1] < - 400) {
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

void set_balancing_gates(uint8_t gates[][MAXCELLS]) {
    uint8_t payload[_numberOfSlaves][PAYLOADLEN];

    for (size_t slave = 0; slave < _numberOfSlaves; slave++){
        payload[slave][0] = 0xFC;
        payload[slave][1] = _undervoltage_reg & 0x00FF;
        payload[slave][2] = ((_undervoltage_reg & 0x0F00) >> 8)
                      | ((_overvoltage_reg & 0x000F) << 4);
        payload[slave][3] = ((_overvoltage_reg & 0x0FF0) >> 4);

        for (size_t cell = 0; cell < 8; cell++) {
            payload[slave][4] |= ((gates[slave][cell] & 0x1) << cell);
        }
        payload[slave][5] = ((gates[slave][8] & 0x1) << 0) | ((gates[slave][9] & 0x1) << 1) |
                            ((gates[slave][10] & 0x1) << 2) | ((gates[slave][11] & 0x1) << 3);
    }
    //TODO remove comment. Just for safety during testing!
    //broadcast(WRCFGA, payload, NULL, NULL);
}

void ltc_config_slaves_task(void *p) {
    (void)p;
    while (1) {
        if(spi_mutex_take(LTC6811_SPI, portMAX_DELAY)) {
            xTaskCreate(ltc6811_worker_task, "ltc_worker", 2000, NULL, _workerTasksPrio, NULL);
            ltc_wake_daisy_chain();
            set_config();
            get_uid(_UID);
            for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
                PRINTF("UID %d: %08X\n", slave+1, _UID[slave]);
            }
            spi_mutex_give(LTC6811_SPI);
            vTaskDelete(NULL);
        }
    }
}

void ltc6811_worker_task(void *p) {
    (void)p;
    battery_data_t stackDataLocal;
    static uint8_t pecVoltage[MAXSTACKS][MAXCELLS];
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        if (spi_mutex_take(LTC6811_SPI, portMAX_DELAY)) {



            ltc_wake_daisy_chain();
            open_wire_check(stackDataLocal.cellVoltageStatus);
            get_voltage(stackDataLocal.cellVoltage, pecVoltage);
            get_temperatures_in_degC(stackDataLocal.temperature, stackDataLocal.temperatureStatus);

            // Error priority:
            // PEC error
            // Open cell wire
            // value out of range


            //memset(&stackDataLocal.cellVoltageStatus, 0, sizeof(stackDataLocal.cellVoltageStatus));
            for (size_t slave = 0; slave < _numberOfSlaves; slave++) {
                // Validity check for temperature sensors
                for (size_t tempsens = 0; tempsens < MAXTEMPSENS; tempsens++) {
                    if ((stackDataLocal.temperature[slave][tempsens] > MAXCELLTEMP) &&
                         stackDataLocal.temperatureStatus[slave][tempsens] != PECERROR) {
                        stackDataLocal.temperatureStatus[slave][tempsens] = VALUEOUTOFRANGE;
                    }

                    if ((stackDataLocal.temperature[slave][tempsens] < 10) &&
                         stackDataLocal.temperatureStatus[slave][tempsens] != PECERROR) {
                         stackDataLocal.temperatureStatus[slave][tempsens] = OPENCELLWIRE;
                    }
                }

                // Validity checks for cell voltage measurement
                for (size_t cell = 0; cell < MAXCELLS; cell++) {
                    // PEC error: highest prio
                    if (pecVoltage[slave][cell] == PECERROR) {
                        stackDataLocal.cellVoltageStatus[slave][cell + 1] = PECERROR;
                        continue;
                    }
                    // Open sensor wire: Prio 2
                    if (stackDataLocal.cellVoltageStatus[slave][cell + 1] == OPENCELLWIRE) {
                        continue;
                    }
                    // Value out of range: Prio 3
                    if ((stackDataLocal.cellVoltage[slave][cell] > _cellOverVoltageThreshold)||
                        (stackDataLocal.cellVoltage[slave][cell] < _cellUnderVoltageThreshold)) {
                            stackDataLocal.cellVoltageStatus[slave][cell + 1] = VALUEOUTOFRANGE;
                    }

                }
            }

            stackDataLocal.bmsStatus = batteryData.bmsStatus;
            stackDataLocal.shutdownStatus = batteryData.shutdownStatus;

            if (batteryData_mutex_take(portMAX_DELAY)) {
                memcpy(&batteryData, &stackDataLocal, sizeof(battery_data_t));
                memcpy(&batteryData.UID, _UID, sizeof(_UID));
                batteryData.packVoltage = 0;
                batteryData.current = 0;
                batteryData.soc = 0;

                batteryData_mutex_give();
            }





            spi_mutex_give(LTC6811_SPI);
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
        }
    }
}

BaseType_t batteryData_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(_batteryDataMutex, blocktime);
}

void batteryData_mutex_give(void) {
    xSemaphoreGive(_batteryDataMutex);
}
