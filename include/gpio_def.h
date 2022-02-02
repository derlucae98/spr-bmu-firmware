/*
 * gpio_def.h
 *
 *  Created on: Jan 29, 2022
 *      Author: scuderia
 */

#ifndef GPIO_DEF_H_
#define GPIO_DEF_H_

#include "S32K146.h"
#include "gpio.h"

#define LED_CARD_PORT       PORTE
#define LED_CARD_PIN        1
#define LED_IMD_FAULT_PORT  PORTE
#define LED_IMD_FAULT_PIN   2
#define LED_IMD_OK_PORT     PORTE
#define LED_IMD_OK_PIN      6
#define LED_WARNING_PORT    PORTA
#define LED_WARNING_PIN     11
#define LED_AMS_FAULT_PORT  PORTA
#define LED_AMS_FAULT_PIN   12
#define LED_AMS_OK_PORT     PORTA
#define LED_AMS_OK_PIN      13

#define CONT_HV_NEG_PORT    PORTA
#define CONT_HV_NEG_PIN     0
#define CONT_HV_POS_PORT    PORTC
#define CONT_HV_POS_PIN     7
#define CONT_HV_PRE_PORT    PORTC
#define CONT_HV_PRE_PIN     2
#define HV_POS_STAT_PORT    PORTA
#define HV_POS_STAT_PIN     7
#define HV_NEG_STAT_PORT    PORTC
#define HV_NEG_STAT_PIN     8
#define HV_PRE_STAT_PORT    PORTA
#define HV_PRE_STAT_PIN     6

#define CS_CARD_PORT        PORTB
#define CS_CARD_PIN         13
#define CS_RTC_PORT         PORTB
#define CS_RTC_PIN          5
#define CS_UBATT_PORT       PORTE
#define CS_UBATT_PIN        4
#define CS_ULINK_PORT       PORTE
#define CS_ULINK_PIN        5
#define CS_EEPROM_PORT      PORTE
#define CS_EEPROM_PIN       8
#define CS_SLAVES_PORT      PORTE
#define CS_SLAVES_PIN       10
#define CS_CURRENT_PORT     PORTE
#define CS_CURRENT_PIN      11

#define CARD_DETECT_PORT    PORTE
#define CARD_DETECT_PIN     0
#define INT_RTC_PORT        PORTC
#define INT_RTC_PIN         3
#define AMS_RES_STAT_PORT   PORTA
#define AMS_RES_STAT_PIN    1
#define IMD_STAT_PORT       PORTB
#define IMD_STAT_PIN        12
#define SC_STATUS_PORT      PORTC
#define SC_STATUS_PIN       6
#define AMS_FAULT_PORT      PORTC
#define AMS_FAULT_PIN       14
#define IMD_RES_STAT_PORT   PORTD
#define IMD_RES_STAT_PIN    3
#define IMD_MEAS_PORT       PORTE
#define IMD_MEAS_PIN        7
#define IMD_ON_DELAY_PORT   PORTE
#define IMD_ON_DELAY_PIN    9
#define CAN_STBY_PORT       PORTC
#define CAN_STBY_PIN        9

#define UART_PORT           PORTA
#define UART_RX             2
#define UART_TX             3

#define CAN_PORT            PORTB
#define CAN_RX              0
#define CAN_TX              1

#define SPI0_PORT           PORTB
#define SPI0_SCK            2
#define SPI0_MISO           3
#define SPI0_MOSI           4
#define SPI1_PORT           PORTD
#define SPI1_SCK            0
#define SPI1_MISO           1
#define SPI1_MOSI           2
#define SPI2_PORT           PORTC
#define SPI2_SCK            15
#define SPI2_MISO           0
#define SPI2_MOSI           1






#endif /* GPIO_DEF_H_ */
