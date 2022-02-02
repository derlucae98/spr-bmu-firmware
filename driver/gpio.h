#ifndef GPIO_H_
#define GPIO_H_

#include "S32K146.h"
#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"

/* S32K uses different macros to access GPIO registers for
 * initialization and pin control.
 * Example:
 * PORTn->... for setup
 * PTn->... for pin control
 * Since we only want to define aliases for the GPIO ports once,
 * we simply convert the PORTn definitions into PTn definitions
 * using a bit of arithmetic. The register addresses are
 * defined in the S32K14x.h
 */
#define _PTn(n) ((GPIO_Type*)(((((uintptr_t)(n)) - PORTA_BASE) >> 6U) + PTA_BASE)) //n = PORTn

enum {
    GPIO_INPUT,
    GPIO_OUTPUT
};

enum {
    PULL_DOWN,
    PULL_UP
};

enum {
    DRIVE_STRENGTH_LOW,
    DRIVE_STRENGTH_HIGH
};

void set_direction(PORT_Type *port, uint8_t pin, uint8_t io);
void enable_pull(PORT_Type *port, uint8_t pin, uint8_t pullType);
void set_drive_strength(PORT_Type *port, uint8_t pin, uint8_t driveStrength);
void set_pin_mux(PORT_Type *port, uint8_t pin, uint8_t mux);
void set_pin(PORT_Type *port, uint8_t pin);
void clear_pin(PORT_Type *port, uint8_t pin);
void toggle_pin(PORT_Type *port, uint8_t pin);
uint8_t get_pin(PORT_Type *port, uint8_t pin);
#endif /* GPIO_H_ */
