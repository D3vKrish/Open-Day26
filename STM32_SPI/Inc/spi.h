#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>

void SPI_Init(void);
void SPI_Clock_Init(void);
void SPI_Send(uint32_t);
uint32_t SPI_Recieve(void);
void DMA_Init(void);
void SPI_Transfer_B2B(void);
void delayMs(uint32_t);

#endif
