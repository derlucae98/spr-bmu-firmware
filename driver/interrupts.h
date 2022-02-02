#ifndef INTERRUPTS_H_
#define INTERRUPTS_H_

#include "FreeRTOS.h"
#include "S32K146.h"
#include "gpio.h"

typedef void (*interrupt_callback_t)(BaseType_t*);
typedef enum {
    IRQ_LOW = 0x8,
    IRQ_EDGE_RISING,
    IRQ_EDGE_FALLING,
    IRQ_EDGE_BOTH,
    IRQ_HIGH
} interrupt_type_t;

void attach_interrupt(PORT_Type *port, uint8_t pin, interrupt_type_t type, interrupt_callback_t callback);
void nvic_set_priority(IRQn_Type IRQn, uint8_t prio);
void nvic_enable_irq(IRQn_Type IRQn);
void nvic_disable_irq(IRQn_Type IRQn);


#endif /* INTERRUPTS_H_ */
