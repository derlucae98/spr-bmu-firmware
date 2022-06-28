#ifndef UART_H_
#define UART_H_

#include <stdio.h>
#include <stdarg.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "interrupts.h"
#include "S32K146.h"
#include "gpio.h"
#include "stdbool.h"

typedef void (*uart_receive_hook_t)(char*); //Function pointer for the uart receive hook

void uart_register_receive_hook(uart_receive_hook_t uartRecHook);
void uart_init(bool enableRecv);
void PRINTF(const char *format, ...);
void uart_rec_task(void *p);
extern TaskHandle_t uartRecTaskHandle;


#endif /* UART_H_ */
