#ifndef SPI_H_
#define SPI_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "S32K146.h"
#include "config.h"

enum {
    LPSPI_PRESC_1,
    LPSPI_PRESC_2,
    LPSPI_PRESC_4,
    LPSPI_PRESC_8,
    LPSPI_PRESC_16,
    LPSPI_PRESC_32,
    LPSPI_PRESC_64,
    LPSPI_PRESC_128
};

enum {
    LPSPI_MODE_0,
    LPSPI_MODE_1,
    LPSPI_MODE_2,
    LPSPI_MODE_3
};

void spi_init(LPSPI_Type *spi, uint8_t presc, uint8_t mode);
uint8_t spi_move(LPSPI_Type *spi, uint8_t b);
void spi_move_array(LPSPI_Type *spi, uint8_t *a, size_t len);
void spi_send_array(LPSPI_Type *spi, const uint8_t *a, size_t len);
BaseType_t spi_mutex_take(LPSPI_Type *spi, TickType_t blockTime);
void spi_mutex_give(LPSPI_Type *spi);


#endif /* SPI_H_ */

