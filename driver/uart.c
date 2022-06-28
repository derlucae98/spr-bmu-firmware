#include "uart.h"

TaskHandle_t uartRecTaskHandle = NULL;

static SemaphoreHandle_t _uartMutex = NULL;
static volatile uint8_t _uartStrComplete = 0;
static volatile uint8_t _uartStrCount = 0;
static volatile char _uartString[128] = "";
static uart_receive_hook_t _uartReceiveHook = NULL;

static void uart_putc(char c);
static void uart_puts(char *s);

void uart_register_receive_hook(uart_receive_hook_t uartRecHook) {
    _uartReceiveHook = uartRecHook;
}

void uart_init(bool enableRecv) {

    PCC->PCCn[PCC_LPUART0_INDEX] &= ~PCC_PCCn_CGC_MASK; //Disable clock
    PCC->PCCn[PCC_LPUART0_INDEX] |= PCC_PCCn_PCS(1)     //SOSCDIV2_CLK as clock source
                                 |  PCC_PCCn_CGC_MASK;  //Enable clock

    LPUART0->BAUD = 0x09000007;     // Initialize for 115200 baud

    if (enableRecv) {
        LPUART0->CTRL = LPUART_CTRL_RIE_MASK | LPUART_CTRL_RE_MASK;

        nvic_set_priority(LPUART0_RxTx_IRQn, 0xFF);
        nvic_enable_irq(LPUART0_RxTx_IRQn);
    }

    LPUART0->CTRL |= LPUART_CTRL_TE_MASK;

    _uartMutex = xSemaphoreCreateMutex();
    configASSERT(_uartMutex);
}

void PRINTF(const char *format, ...) {
    va_list arg;
    char buffer[128];
    va_start (arg, format);
    vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end (arg);
    uart_puts(buffer);
}

void uart_rec_task(void *p) {
    (void)p;
    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            if (_uartReceiveHook != NULL) {
                (*_uartReceiveHook)((char*)&_uartString);
            }
            _uartStrComplete = 0;
        }
    }
}

void LPUART0_RxTx_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    char nextChar;
    if ((LPUART0->STAT & LPUART_STAT_RDRF_MASK) && !(LPUART0->STAT & LPUART_STAT_FE_MASK)) {
        nextChar = LPUART0->DATA;
        if(_uartStrComplete == 0) {
            if(nextChar != '\n' && _uartStrCount < 128 ) {
                _uartString[_uartStrCount] = nextChar;
                _uartStrCount++;
            } else {
                _uartString[_uartStrCount] = '\0';
                _uartStrCount = 0;
                _uartStrComplete = 1;

                if (uartRecTaskHandle != NULL) {
                    vTaskNotifyGiveFromISR(uartRecTaskHandle, &xHigherPriorityTaskWoken);
                }
            }
        }
    } else {
        //Suppress spurious interrupt after reset, which fills _uartString[0] with garbage
        LPUART0->STAT = LPUART0->STAT; //Clear all status flags
        (void)LPUART0->DATA; //Read data register to clear RDRF irq flag
        for (size_t i = 0; i < sizeof(_uartString); i++) {
            _uartString[i] = 0; //Clear string
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void uart_putc(char c) {
    while(!(LPUART0->STAT & LPUART_STAT_TDRE_MASK)); //Wait for transmit buffer to be empty
    LPUART0->DATA = c;
    while(!(LPUART0->STAT & LPUART_STAT_TC_MASK)); //Wait for transmission complete
}

static void uart_puts(char* s) {
    if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t i = 0;
        while(s[i] != '\0')  {
            uart_putc(s[i]);
            i++;
        }
        xSemaphoreGive(_uartMutex);
    }
}

