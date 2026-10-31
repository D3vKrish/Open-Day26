#include <stdint.h>
#include "stm32f4xx.h"
#include "gpio.h"
#include "uart.h"

uint32_t msTicks = 0;

// Function prototypes
void setUpDevice(void);
void setClockTo32Mhz(void);
void delayMs(uint32_t);

void setupSPI(void);
void spi_send(uint8_t data);

#define TRANSFER_SIZE 64
uint8_t rx_buffer[TRANSFER_SIZE];
uint8_t tx_buffer[TRANSFER_SIZE];
uint8_t dummy_buffer[TRANSFER_SIZE] = {0}; // Bit-bucket for full-duplex transfers

void SPI_Init(void) {
    // 1. Enable clocks for GPIOA and SPI1
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // 2. Configure PA4 as GPIO Output (CS), idle HIGH
    GPIOA->ODR |= (1 << 4);
    GPIO_Init('A', 4, 1); // Output

    // 3. Configure PA5 (SCK), PA6 (MISO), PA7 (MOSI) as Alternate Function (AF5)
    GPIO_Init('A', 5, 2);
    GPIO_Init('A', 6, 2);
    GPIO_Init('A', 7, 2);

    // CRITICAL: Very High output speed (11) on PA4, PA5, PA6, PA7
    // Overcomes the Nucleo LD2 510-ohm load on PA5
    GPIOA->OSPEEDR |= (3U << 8) | (3U << 10) | (3U << 12) | (3U << 14);

    // Cleanly clear and set AF5 for pins 5, 6, 7
    GPIOA->AFR[0] &= ~((0xFU << 20) | (0xFU << 24) | (0xFU << 28));
    GPIOA->AFR[0] |=  ((5U   << 20) | (5U   << 24) | (5U   << 28));

    // Pull-down on SCK so it idles cleanly at 0V (Mode 0)
    GPIOA->PUPDR &= ~((3U << 10));
    GPIOA->PUPDR |=  (2U << 10);

    // 4. Configure SPI1: Master mode, Mode 0 (CPOL=0, CPHA=0), 8-bit, SSM=1, SSI=1
    // BR = 5 (Div 64 ~= 250 kHz) for fast, clean, noise-free clocking with ESP32
    SPI1->CR1 = (1 << 9) | (1 << 8) | (1 << 2) | (5 << 3);

    // 5. Enable TX and RX DMA requests in SPI1
    SPI1->CR2 = (1 << 0) | (1 << 1);
}

void SPI_Transfer_B2B(void) {
    // 1. Disable DMA streams and wait until hardware confirms
    DMA2_Stream2->CR &= ~(1 << 0);
    DMA2_Stream3->CR &= ~(1 << 0);
    while (DMA2_Stream2->CR & (1 << 0));
    while (DMA2_Stream3->CR & (1 << 0));

    // 2. Clear all interrupt and error flags for Streams 2 and 3
    DMA2->LIFCR = (0x3D << 16) | (0x3D << 22);

    // 3. Configure memory addresses: TX = tx_buffer, RX = rx_buffer (Full Duplex)
    DMA2_Stream3->M0AR = (uint32_t)tx_buffer;       // Send 64-byte flit data
    DMA2_Stream3->NDTR = TRANSFER_SIZE;

    DMA2_Stream2->M0AR = (uint32_t)rx_buffer;       // Receive ESP32 ACK directly
    DMA2_Stream2->NDTR = TRANSFER_SIZE;

    // 4. Assert CS Low
    GPIOA->ODR &= ~(1 << 4);

    // 5. Enable RX DMA, TX DMA, then SPI Master (SPE = 1)
    DMA2_Stream2->CR |= (1 << 0);       // Enable RX DMA
    DMA2_Stream3->CR |= (1 << 0);       // Enable TX DMA
    SPI1->CR1 |= (1 << 6);              // Enable SPI (SPE = 1)

    // 6. Wait for TX complete, RX complete, and SPI not busy
    while (!(DMA2->LISR & (1 << 27)));
    while (!(DMA2->LISR & (1 << 21)));
    while (SPI1->SR & (1 << 7));

    // 7. Deassert CS High & Disable SPI
    GPIOA->ODR |= (1 << 4);
    SPI1->CR1 &= ~(1 << 6);
}

void delayMs(uint32_t delay) {
    // Microsecond-accurate, non-blocking wrap-safe delay using TIM2 hardware timer (1 MHz tick)
    uint32_t start = TIM2->CNT;
    uint32_t wait_us = delay * 1000;
    while ((TIM2->CNT - start) < wait_us);
}

void SPI_Send(uint8_t data){
	SPI1->DR = data;
	while((SPI1->SR & SPI_SR_TXE)!= SPI_SR_TXE);
}

void DMA_Init(void){
	// Enable DMA2
	RCC->AHB1ENR |= (1<<22);

	// Receiving configuration (Stream 2, Channel 3, 8-bit, MINC)
	DMA2_Stream2->CR &= ~((3<<11) | (3<<13));
	DMA2_Stream2->CR |= ((3 << 25) | (1 << 10));
	DMA2_Stream2->PAR = (uint32_t)&SPI1->DR;
	DMA2_Stream2->M0AR = (uint32_t)rx_buffer;
	DMA2_Stream2->NDTR = TRANSFER_SIZE;

	// Transmitting Configuration (Stream 3, Channel 3, 8-bit, MINC, DIR=Memory-to-Peripheral)
	DMA2_Stream3->CR &= ~((3<<11) | (3<<13));
	DMA2_Stream3->CR |= ((3<<25) | (1<<10) | (1<<6));
	DMA2_Stream3->NDTR = TRANSFER_SIZE;
	DMA2_Stream3->PAR = (uint32_t)&SPI1->DR;
	DMA2_Stream3->M0AR = (uint32_t)tx_buffer;
}

uint32_t SPI_Recieve(){
	DMA2_Stream2->CR &= ~(1<<0);
	DMA2_Stream3->CR &= ~(1<<0);
	while(DMA2_Stream2->CR & (1<<0));
	while(DMA2_Stream3->CR & (1<<0));
	DMA2_Stream2->NDTR = 0x03;
	DMA2_Stream3->NDTR = 0x03;

	DMA2->LIFCR |= (1<<21);

	GPIOA->ODR &= ~(1<<4);

	DMA2_Stream2->CR |= (1<<0);
	DMA2_Stream3->CR |= (1<<0);
	SPI1->CR1 |= (1<<6);

	while(!(DMA2->LISR & (1<<21))){};
	GPIOA->ODR |= (1<<4);
	SPI1->CR1 &= ~(1<<6);
	DMA2->LIFCR |= (1<<21);
	uint32_t position = (rx_buffer[1] << 5) | (rx_buffer[2] >> 11);
	uint32_t angle = (position*360)/(2097152);
	return angle;
}
