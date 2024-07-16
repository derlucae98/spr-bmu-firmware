#include "interrupts.h"

#define NUMOFPINS_A 32
#define NUMOFPINS_B 32
#define NUMOFPINS_C 32
#define NUMOFPINS_D 32
#define NUMOFPINS_E 32
static interrupt_callback_t callbacksA[NUMOFPINS_A];
static interrupt_callback_t callbacksB[NUMOFPINS_B];
static interrupt_callback_t callbacksC[NUMOFPINS_C];
static interrupt_callback_t callbacksD[NUMOFPINS_D];
static interrupt_callback_t callbacksE[NUMOFPINS_E];

static void handle_interrupt(PORT_Type *port);

void attach_interrupt(PORT_Type *port, uint8_t pin, interrupt_type_t type, interrupt_callback_t callback) {
    uintptr_t _port = (uintptr_t)port; //Get the address of the pointer as a number
    switch (_port) {
    case PORTA_BASE:
        configASSERT(pin <= NUMOFPINS_A);
        callbacksA[pin] = callback;
        //Enable interrupt for PORTA
        nvic_set_priority(PORTA_IRQn, 0xFF);
        nvic_enable_irq(PORTA_IRQn);
        break;
    case PORTB_BASE:
        configASSERT(pin <= NUMOFPINS_B);
        callbacksB[pin] = callback;
        //Enable interrupt for PORTB
        nvic_set_priority(PORTB_IRQn, 0xFF);
        nvic_enable_irq(PORTB_IRQn);
        break;
    case PORTC_BASE:
        configASSERT(pin <= NUMOFPINS_C);
        callbacksC[pin] = callback;
        //Enable interrupt for PORTC
        nvic_set_priority(PORTC_IRQn, 0xFF);
        nvic_enable_irq(PORTC_IRQn);
        break;
    case PORTD_BASE:
        configASSERT(pin <= NUMOFPINS_D);
        callbacksD[pin] = callback;
        //Enable interrupt for PORTD
        nvic_set_priority(PORTD_IRQn, 0xFF);
        nvic_enable_irq(PORTD_IRQn);
        break;
    case PORTE_BASE:
        configASSERT(pin <= NUMOFPINS_E);
        callbacksE[pin] = callback;
        //Enable interrupt for PORTE
        nvic_set_priority(PORTE_IRQn, 0xFF);
        nvic_enable_irq(PORTE_IRQn);
        break;
    default:
        configASSERT(0);
    }

    //Mask the pin in the interrupt register
    port->PCR[pin] |= PORT_PCR_IRQC(type);
}

void nvic_set_priority(IRQn_Type IRQn, uint8_t prio) {
    S32_NVIC->IP[IRQn] = prio;
}

void nvic_enable_irq(IRQn_Type IRQn) {
    //See reference manual section 7.2.3 and S32K1xx_DMA_Interrupt_mapping.xlsx
    S32_NVIC->ICPR[IRQn / 32] = 1 << (IRQn % 32);
    S32_NVIC->ISER[IRQn / 32] = 1 << (IRQn % 32);
}

void nvic_disable_irq(IRQn_Type IRQn) {
    //See reference manual section 7.2.3 and S32K1xx_DMA_Interrupt_mapping.xlsx
    S32_NVIC->ICPR[IRQn / 32] = 1 << (IRQn % 32);
    S32_NVIC->ICER[IRQn / 32] = 1 << (IRQn % 32);
}

//IRQ handler are defined in startup_S32K146.S
void PORTA_IRQHandler() {
    handle_interrupt(PORTA);
}

void PORTB_IRQHandler() {
    handle_interrupt(PORTB);
}

void PORTC_IRQHandler() {
    handle_interrupt(PORTC);
}

void PORTD_IRQHandler() {
    handle_interrupt(PORTD);
}

void PORTE_IRQHandler() {
    handle_interrupt(PORTE);
}

static void handle_interrupt(PORT_Type *port) {
    uintptr_t _port = (uintptr_t)port;
    uint32_t irqFlag;
    BaseType_t higherPrioTaskWoken = pdFALSE;
    for (size_t i = 32; i-- > 0;) {
        //Check where the interrupt came from and invoke callback
        irqFlag = (port->ISFR >> i) & 0x1;
        if (irqFlag) {
            switch (_port) {
            case PORTA_BASE:
                if (callbacksA[i]) {
                    (callbacksA[i])(&higherPrioTaskWoken);
                }
                break;
            case PORTB_BASE:
                if (callbacksB[i]) {
                    (callbacksB[i])(&higherPrioTaskWoken);
                }
                break;
            case PORTC_BASE:
                if (callbacksC[i]) {
                    (callbacksC[i])(&higherPrioTaskWoken);
                }
                break;
            case PORTD_BASE:
                if (callbacksD[i]) {
                    (callbacksD[i])(&higherPrioTaskWoken);
                }
                break;
            case PORTE_BASE:
                if (callbacksE[i]) {
                    (callbacksE[i])(&higherPrioTaskWoken);
                }
                break;
            default:
                configASSERT(0);
            }
        }
    }
    port->ISFR = port->ISFR; //Clear interrupts
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}


