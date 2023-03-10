/*
 * config.h
 *
 *  Created on: Mar 10, 2023
 *      Author: Luca Engelmann
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#define VERS_MAJOR 1
#define VERS_MINOR 0
#define VERS_BUILD 0


#define NUMBEROFSLAVES 12
#define CELL_UNDERVOLTAGE 3000
#define CELL_OVERVOLTAGE  4166


#define SOC_TASK_STACK 512
#define SOC_TASK_PRIO  2


#define EEPROM_SOC_PAGE_1 0x100
#define EEPROM_SOC_PAGE_2 0x200
#define EEPROM_SOC_PAGE_3 0x300

#define NOMINAL_CELL_CAPACITY_mAh 15000


#define MAXSTACKS 12
#define MAXCELLS  12
#define MAXTEMPSENS 14
#define MAXCELLTEMP 560 //56 deg C

#define BMU_Q_HANDLE can0RxQueueHandle

// ## I/O ##

//LEDs
#define LED_IMD_FAULT_PORT  PORTC
#define LED_IMD_FAULT_PIN   13
#define LED_IMD_OK_PORT     PORTB
#define LED_IMD_OK_PIN      29

#define LED_AMS_FAULT_PORT  PORTC
#define LED_AMS_FAULT_PIN   19
#define LED_AMS_OK_PORT     PORTC
#define LED_AMS_OK_PIN      12

#define LED_WARNING_PORT    PORTB
#define LED_WARNING_PIN     2

#define LED_CARD_PORT       PORTA
#define LED_CARD_PIN        12

//AIR
#define AIR_POS_SET_PORT    PORTD //Set AIR+ latch
#define AIR_POS_SET_PIN     29
#define AIR_POS_CLR_PORT    PORTA //Clear AIR+ latch
#define AIR_POS_CLR_PIN     1
#define AIR_POS_INTENT_PORT PORTA //Current state of AIR+ latch
#define AIR_POS_INTENT_PIN  3
#define AIR_POS_STATE_PORT  PORTD //Mechanical state of AIR+
#define AIR_POS_STATE_PIN   22

#define AIR_NEG_SET_PORT    PORTD //Set AIR- latch
#define AIR_NEG_SET_PIN     24
#define AIR_NEG_CLR_PORT    PORTB //Clear AIR- latch
#define AIR_NEG_CLR_PIN     10
#define AIR_NEG_INTENT_PORT PORTD //Current state of AIR- latch
#define AIR_NEG_INTENT_PIN  2
#define AIR_NEG_STATE_PORT  PORTD //Mechanical state of AIR-
#define AIR_NEG_STATE_PIN   3

#define AIR_PRE_SET_PORT    PORTA //Set PRE latch
#define AIR_PRE_SET_PIN     2
#define AIR_PRE_CLR_PORT    PORTB //Clear PRE latch
#define AIR_PRE_CLR_PIN     11
#define AIR_PRE_INTENT_PORT PORTD //Current state of PRE latch
#define AIR_PRE_INTENT_PIN  23
#define AIR_PRE_STATE_PORT  PORTD //Mechanical state of Precharge AIR
#define AIR_PRE_STATE_PIN   4

//SPI
#define SPI0_SCK_PORT       PORTD
#define SPI0_SCK_PIN        15
#define SPI0_MISO_PORT      PORTD
#define SPI0_MISO_PIN       16
#define SPI0_MOSI_PORT      PORTA
#define SPI0_MOSI_PIN       30

#define SPI1_SCK_PORT       PORTB
#define SPI1_SCK_PIN        14
#define SPI1_MISO_PORT      PORTB
#define SPI1_MISO_PIN       15
#define SPI1_MOSI_PORT      PORTB
#define SPI1_MOSI_PIN       16

#define SPI2_SCK_PORT       PORTE
#define SPI2_SCK_PIN        15
#define SPI2_MISO_PORT      PORTE
#define SPI2_MISO_PIN       16
#define SPI2_MOSI_PORT      PORTA
#define SPI2_MOSI_PIN       8

//SD Card
#define CARD_DETECT_PORT    PORTE
#define CARD_DETECT_PIN     25
#define CS_CARD_PORT        PORTE
#define CS_CARD_PIN         24
#define SCK_CARD_PORT       SPI2_SCK_PORT
#define SCK_CARD_PIN        SPI2_SCK_PIN
#define MISO_CARD_PORT      SPI2_MISO_PORT
#define MISO_CARD_PIN       SPI2_MISO_PIN
#define MOSI_CARD_PORT      SPI2_MOSI_PORT
#define MOSI_CARD_PIN       SPI2_MOSI_PIN

//RTC
#define CS_RTC_PORT         PORTD //Chip select for RTC is active high!
#define CS_RTC_PIN          1
#define CLKOUT_RTC_PORT     PORTE
#define CLKOUT_RTC_PIN      11
#define IRQ_RTC_PORT        PORTD
#define IRQ_RTC_PIN         0
#define SCK_RTC_PORT        SPI2_SCK_PORT
#define SCK_RTC_PIN         SPI2_SCK_PIN
#define MISO_RTC_PORT       SPI2_MISO_PORT
#define MISO_RTC_PIN        SPI2_MISO_PIN
#define MOSI_RTC_PORT       SPI2_MOSI_PORT
#define MOSI_RTC_PIN        SPI2_MOSI_PIN

//EEPROM
#define CS_EEPROM_PORT      PORTA
#define CS_EEPROM_PIN       11
#define SCK_EEPROM_PORT     SPI2_SCK_PORT
#define SCK_EEPROM_PIN      SPI2_SCK_PIN
#define MISO_EEPROM_PORT    SPI2_MISO_PORT
#define MISO_EEPROM_PIN     SPI2_MISO_PIN
#define MOSI_EEPROM_PORT    SPI2_MOSI_PORT
#define MOSI_EEPROM_PIN     SPI2_MOSI_PIN

//ADC
#define CS_ADC_PORT         PORTB
#define CS_ADC_PIN          13
#define IRQ_ADC_PORT        PORTB
#define IRQ_ADC_PIN         17
#define SCK_ADC_PORT        SPI1_SCK_PORT
#define SCK_ADC_PIN         SPI1_SCK_PIN
#define MISO_ADC_PORT       SPI1_MISO_PORT
#define MISO_ADC_PIN        SPI1_MISO_PIN
#define MOSI_ADC_PORT       SPI1_MOSI_PORT
#define MOSI_ADC_PIN        SPI1_MOSI_PIN

//Slaves
#define CS_SLAVES_PORT      PORTE
#define CS_SLAVES_PIN       12
#define SCK_SLAVES_PORT     SPI0_SCK_PORT
#define SCK_SLAVES_PIN      SPI0_SCK_PIN
#define MISO_SLAVES_PORT    SPI0_MISO_PORT
#define MISO_SLAVES_PIN     SPI0_MISO_PIN
#define MOSI_SLAVES_PORT    SPI0_MOSI_PORT
#define MOSI_SLAVES_PIN     SPI0_MOSI_PIN

//Status
#define AMS_RES_STAT_PORT   PORTB
#define AMS_RES_STAT_PIN    9
#define IMD_RES_STAT_PORT   PORTD
#define IMD_RES_STAT_PIN    27
#define IMD_STAT_PORT       PORTE
#define IMD_STAT_PIN        22
#define SC_STATUS_PORT      PORTB
#define SC_STATUS_PIN       12

//IMD PWM signal
#define IMD_MEAS_PORT       PORTE
#define IMD_MEAS_PIN        9

//AMS Shutdown Circuit control
#define AMS_FAULT_PORT      PORTE
#define AMS_FAULT_PIN       21

//TS voltage detection
#define TSAC_HV_PORT        PORTA
#define TSAC_HV_PIN         17

//CAN
#define CAN_DIAG_PORT       PORTC
#define CAN_DIAG_RX         6
#define CAN_DIAG_TX         7
#define CAN_VEHIC_PORT      PORTB
#define CAN_VEHIC_RX        0
#define CAN_VEHIC_TX        1

//UART
#define UART_PORT           PORTC
#define UART_RX             2
#define UART_TX             3

//Debug
#define DBG_1_PORT          PORTA
#define DBG_1_PIN           7
#define DBG_2_PORT          PORTC
#define DBG_2_PIN           29
#define DBG_3_PORT          PORTC
#define DBG_3_PIN           8
#define DBG_4_PORT          PORTC
#define DBG_4_PIN           9
#define DBG_5_PORT          PORTC
#define DBG_5_PIN           28
#define DBG_6_PORT          PORTC
#define DBG_6_PIN           27
#define DBG_7_PORT          PORTC
#define DBG_7_PIN           10
#define DBG_8_PORT          PORTC
#define DBG_8_PIN           11
#define DBG_9_PORT          PORTC
#define DBG_9_PIN           23

#define dbg1(x) (x ? set_pin : clear_pin)(DBG_1_PORT, DBG_1_PIN);
#define dbg2(x) (x ? set_pin : clear_pin)(DBG_2_PORT, DBG_2_PIN);
#define dbg3(x) (x ? set_pin : clear_pin)(DBG_3_PORT, DBG_3_PIN);
#define dbg4(x) (x ? set_pin : clear_pin)(DBG_4_PORT, DBG_4_PIN);
#define dbg5(x) (x ? set_pin : clear_pin)(DBG_5_PORT, DBG_5_PIN);
#define dbg6(x) (x ? set_pin : clear_pin)(DBG_6_PORT, DBG_6_PIN);
#define dbg7(x) (x ? set_pin : clear_pin)(DBG_7_PORT, DBG_7_PIN);
#define dbg8(x) (x ? set_pin : clear_pin)(DBG_8_PORT, DBG_8_PIN);
#define dbg9(x) (x ? set_pin : clear_pin)(DBG_9_PORT, DBG_9_PIN);


#endif /* CONFIG_H_ */
