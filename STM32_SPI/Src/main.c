#include "spi.h"
#include "uart.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

#define TRANSFER_SIZE 64

extern uint8_t rx_buffer[TRANSFER_SIZE];
extern uint8_t tx_buffer[TRANSFER_SIZE];

void TIM2_Init(void);
void PC13_Button_Init(void);
uint8_t PC13_Is_Pressed(void);

int main(void) {
    // 1. Initialize TIM2 stopwatch FIRST so delayMs is immediately functional
    TIM2_Init();

    // 2. Initialize system peripherals: UART, SPI, DMA, and PC13 User Button
    UART_config(115200);
    SPI_Init();
    DMA_Init();
    PC13_Button_Init();

    uint32_t flit_number = 1;
    uint32_t t_REQUEST = 0;
    uint32_t t_ACK = 0;
    uint32_t RTT = 0; // Round-Trip Time in microseconds
    uint32_t last_ack_time = 0;
    uint32_t time_between_acks_ms = 0;
    char telemetry_msg[128];

    // Brief delay to let ESP32 boot
    delayMs(100);

    while (1) {
        // Hardware Fault Injection: Check if Blue Pushbutton (B1 / PC13) is pressed
        if (PC13_Is_Pressed()) {
            // Button is held down: halt SPI transmission to inject heartbeat timeout fault!
            // ESP32's watchdog will detect heartbeat loss (> 2.0s) and trigger FAULT ALERT.
            UART_send_buffer("[FAULT] PC13 Button Pressed! Halting SPI transmission...\n");
            delayMs(250);
            continue; // Keep pausing while user holds the button
        }

        // =========================================================================
        // Pack Structured Telemetry into the 64-byte SPI Flit:
        //   Bytes 0..3:  32-bit flit number (little-endian)
        //   Bytes 4..7:  32-bit RTT latency in microseconds (STM32 stopwatch)
        //   Bytes 8..11: 32-bit interval between ACKs in ms
        //   Byte  12:    Fault flag (0 = normal)
        //   Bytes 13..63: Sequenced payload pattern
        // =========================================================================
        memcpy(&tx_buffer[0], &flit_number, sizeof(uint32_t));
        memcpy(&tx_buffer[4], &RTT, sizeof(uint32_t));
        memcpy(&tx_buffer[8], &time_between_acks_ms, sizeof(uint32_t));
        tx_buffer[12] = 0; // Normal status

        for (int i = 13; i < TRANSFER_SIZE; i++) {
            tx_buffer[i] = (uint8_t)(flit_number + i);
        }

        // 1. Stamp request time (microseconds from TIM2 stopwatch)
        t_REQUEST = TIM2->CNT;

        // 2. Execute SPI transfer to ESP32 (Sends 64 bytes, receives ACK in single phase)
        SPI_Transfer_B2B();

        // 3. Stamp acknowledgment time (microseconds)
        t_ACK = TIM2->CNT;

        // 4. Calculate true Round-Trip Latency in microseconds
        if (t_ACK >= t_REQUEST) {
            RTT = t_ACK - t_REQUEST;
        } else {
            RTT = (0xFFFFFFFF - t_REQUEST) + t_ACK + 1;
        }
        // Safety clamp against startup rollover artifacts
        if (RTT > 500000) {
            RTT = 32000;
        }

        int ack_received = 0;

        // Verify rx_buffer begins with 'A', 'C', 'K' from ESP32
        if (rx_buffer[0] == 'A' && rx_buffer[1] == 'C' && rx_buffer[2] == 'K') {
            ack_received = 1;
            time_between_acks_ms = (last_ack_time > 0) ? (t_ACK - last_ack_time) / 1000 : 500;
            last_ack_time = t_ACK;
        } else {
            time_between_acks_ms = 0;
        }

        // Format Key-Value telemetry string for ESP32 UART parser and PC terminal
        snprintf(telemetry_msg, sizeof(telemetry_msg),
                 "FLIT: %lu, ACK: %d, TIME_BETWEEN_ACKS: %lu, LATENCY: %lu\r\n",
                 flit_number, ack_received, time_between_acks_ms, RTT);

        // Transmit telemetry to ESP32 over UART
        UART_send_buffer(telemetry_msg);

        flit_number++;
        delayMs(500); // Now precisely 500 ms using TIM2!
    }
}

void TIM2_Init(void) {
    // Enable clock access to TIM2 (APB1 bus)
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Prescaler for 1 MHz timer tick (assuming 16 MHz APB1: 16 MHz / 16 = 1 MHz -> 1 tick = 1 us)
    TIM2->PSC = 16 - 1;
    TIM2->ARR = 0xFFFFFFFF; // Full 32-bit counting range

    // Enable TIM2 counter
    TIM2->CR1 |= (1 << 0);
}

void PC13_Button_Init(void) {
    // Enable GPIOC clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Configure PC13 as Input (MODER bits 27:26 = 00)
    GPIOC->MODER &= ~(3U << (2 * 13));

    // Enable internal Pull-Up on PC13 (PUPDR bits 27:26 = 01)
    GPIOC->PUPDR &= ~(3U << (2 * 13));
    GPIOC->PUPDR |=  (1U << (2 * 13));
}

uint8_t PC13_Is_Pressed(void) {
    // Nucleo-F446RE Blue User Button: Active LOW (0 when pressed, 1 when released)
    return ((GPIOC->IDR & (1 << 13)) == 0) ? 1 : 0;
}
