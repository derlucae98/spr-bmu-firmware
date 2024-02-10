#include "gpio.h"

void gpio_enable_clock(PORT_Type *port) {
    uintptr_t portType = (uintptr_t)port;
    uint32_t index = (portType >> 12u) & 0xFF;

    PCC->PCCn[index] = PCC_PCCn_CGC_MASK;
}

void set_direction(PORT_Type *port, uint8_t pin, uint8_t io) {
    port->PCR[pin] |= PORT_PCR_MUX(1); //Enable pin in any case
    if (io == GPIO_OUTPUT) {
        _PTn(port)->PDDR |= (1 << pin);
    } else {
        _PTn(port)->PDDR &= ~(1 << pin);
    }
}

void enable_pull(PORT_Type *port, uint8_t pin, uint8_t pullType) {
    if (pullType == PULL_NONE) {
        return;
    }
    port->PCR[pin] |= PORT_PCR_PE_MASK | PORT_PCR_PS(pullType & 0x1);
}

void set_drive_strength(PORT_Type *port, uint8_t pin, uint8_t driveStrength) {
    port->PCR[pin] |= PORT_PCR_DSE(driveStrength & 0x1);
}

void set_pin_mux(PORT_Type *port, uint8_t pin, uint8_t mux) {
    port->PCR[pin] |= PORT_PCR_MUX(mux);
}

void set_pin(PORT_Type *port, uint8_t pin) {
    _PTn(port)->PSOR = (1 << pin);
}

void clear_pin(PORT_Type *port, uint8_t pin) {
    _PTn(port)->PCOR = (1 << pin);
}

void toggle_pin(PORT_Type *port, uint8_t pin) {
    _PTn(port)->PTOR = (1 << pin);
}

uint8_t get_pin(PORT_Type *port, uint8_t pin) {
    return (_PTn(port)->PDIR & (1 << pin)) != 0;
}
