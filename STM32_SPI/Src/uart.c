#include "stm32f4xx.h"
#include <stdint.h>
#include "uart.h"

// Dual UART configuration:
// - USART2 on PA2 (TX) & PA3 (RX) -> ST-LINK USB Virtual COM Port (PC Serial Monitor)
// - USART1 on PA9 (TX / Pin D8) & PA10 (RX / Pin D2) -> Direct jumper to ESP32 (GPIO 16)
void UART_config(uint32_t baud)
{
    if(baud == 0) return;

    // 1. Enable peripheral clocks for USART1 (APB2), USART2 (APB1), and GPIOA (AHB1)
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // -------------------------------------------------------------
    // Configure USART2: PA2 (TX), PA3 (RX) for ST-LINK USB Debugger
    // -------------------------------------------------------------
    GPIOA->MODER &= ~((3U << (2 * 2)) | (3U << (2 * 3)));
    GPIOA->MODER |=  (2U << (2 * 2)) | (2U << (2 * 3));      // Alternate function
    GPIOA->OTYPER &= ~((1U << 2) | (1U << 3));               // Push-pull
    GPIOA->OSPEEDR &= ~((3U << (2 * 2)) | (3U << (2 * 3)));
    GPIOA->OSPEEDR |=  (3U << (2 * 2)) | (3U << (2 * 3));    // High speed
    GPIOA->PUPDR &= ~((3U << (2 * 2)) | (3U << (2 * 3)));    // No pull
    GPIOA->AFR[0] &= ~((0xFU << (4 * 2)) | (0xFU << (4 * 3)));
    GPIOA->AFR[0] |=  (7U << (4 * 2)) | (7U << (4 * 3));     // AF7

    USART2->CR1 &= ~USART_CR1_UE;
    USART2->CR1 &= ~USART_CR1_M;
    USART2->BRR = (16000000 / baud);
    USART2->CR2 &= ~USART_CR2_STOP;
    USART2->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);

    // -------------------------------------------------------------
    // Configure USART1: PA9 (TX / D8), PA10 (RX / D2) for ESP32
    // -------------------------------------------------------------
    GPIOA->MODER &= ~((3U << (2 * 9)) | (3U << (2 * 10)));
    GPIOA->MODER |=  (2U << (2 * 9)) | (2U << (2 * 10));     // Alternate function
    GPIOA->OTYPER &= ~((1U << 9) | (1U << 10));              // Push-pull
    GPIOA->OSPEEDR &= ~((3U << (2 * 9)) | (3U << (2 * 10)));
    GPIOA->OSPEEDR |=  (3U << (2 * 9)) | (3U << (2 * 10));   // High speed
    GPIOA->PUPDR &= ~((3U << (2 * 9)) | (3U << (2 * 10)));   // No pull
    GPIOA->AFR[1] &= ~((0xFU << (4 * 1)) | (0xFU << (4 * 2)));
    GPIOA->AFR[1] |=  (7U << (4 * 1)) | (7U << (4 * 2));     // AF7 (AFRH pin 9=index 1, pin 10=index 2)

    USART1->CR1 &= ~USART_CR1_UE;
    USART1->CR1 &= ~USART_CR1_M;
    USART1->BRR = (16000000 / baud);
    USART1->CR2 &= ~USART_CR2_STOP;
    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}

void UART_send_byte(char data)
{
    // Transmit simultaneously on USART2 (ST-LINK USB / PA2) and USART1 (External Header PA9 / D8)
    while(!(USART2->SR & USART_SR_TXE));
    USART2->DR = (data & 0xFF);

    while(!(USART1->SR & USART_SR_TXE));
    USART1->DR = (data & 0xFF);
}

void UART_send_buffer(const char* buffer)
{
    while(*buffer)
    {
        UART_send_byte(*buffer++);
    }
    while(!(USART2->SR & USART_SR_TC));
    while(!(USART1->SR & USART_SR_TC));
}

void UART_send_uint32(uint32_t number)
{
    if(number == 0)
    {
        UART_send_buffer("0\r\n");
        return;
    }
    char buffer[20] = {(uint8_t) 0};
    uint8_t count = 0;

    while(number != 0)
    {
        buffer[count++] = number % 10 + '0';
        number /= 10;
    }

    for(int i = 0; i < count / 2; i++)
    {
        char temp = buffer[count - 1 - i];
        buffer[count - 1 - i] = buffer[i];
        buffer[i] = temp;
    }

    buffer[count++] = '\r';
    buffer[count++] = '\n';
    buffer[count++] = '\0';

    UART_send_buffer(buffer);
}

void UART_send_hex8(uint8_t number)
{
    char buffer[20] = {(uint8_t)'0'};

    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2] = (((number >> 4) & 0xF) < 10) ? (((number >> 4) & 0xF) + '0') : (((number >> 4) & 0xF) - 10 + 'A');
    buffer[3] = (((number >> 0) & 0xF) < 10) ? (((number >> 0) & 0xF) + '0') : (((number >> 0) & 0xF) - 10 + 'A');
    buffer[4] = '\r';
    buffer[5] = '\n';
    buffer[6] = '\0';

    UART_send_buffer(buffer);
}

void UART_send_hex16(uint16_t number)
{
    char buffer[20] = {(uint8_t) '0'};

    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2] = (((number >> 12) & 0xF) < 10) ? ((number >> 12) & 0xF) + '0' : (((number >> 12) & 0xF) - 10 + 'A');
    buffer[3] = (((number >> 8) & 0xF) < 10) ? ((number >> 8) & 0xF) + '0' : (((number >> 8) & 0xF) - 10 + 'A');
    buffer[4] = (((number >> 4) & 0xF) < 10) ? ((number >> 4) & 0xF) + '0' : (((number >> 4) & 0xF) - 10 + 'A');
    buffer[5] = (((number >> 0) & 0xF) < 10) ? ((number >> 0) & 0xF) + '0' : (((number >> 0) & 0xF) - 10 + 'A');
    buffer[6] = '\r';
    buffer[7] = '\n';
    buffer[8] = '\0';

    UART_send_buffer(buffer);
}

void UART_send_hex32(uint32_t number)
{
    char buffer[20] = {(uint8_t)'0'};

    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2] = (((number >> 28) & 0xF) < 10) ? (((number >> 28) & 0xF) + '0') : (((number >> 28) & 0xF) - 10 + 'A');
    buffer[3] = (((number >> 24) & 0xF) < 10) ? (((number >> 24) & 0xF) + '0') : (((number >> 24) & 0xF) - 10 + 'A');
    buffer[4] = (((number >> 20) & 0xF) < 10) ? (((number >> 20) & 0xF) + '0') : (((number >> 20) & 0xF) - 10 + 'A');
    buffer[5] = (((number >> 16) & 0xF) < 10) ? (((number >> 16) & 0xF) + '0') : (((number >> 16) & 0xF) - 10 + 'A');
    buffer[6] = (((number >> 12) & 0xF) < 10) ? (((number >> 12) & 0xF) + '0') : (((number >> 12) & 0xF) - 10 + 'A');
    buffer[7] = (((number >>  8) & 0xF) < 10) ? (((number >>  8) & 0xF) + '0') : (((number >>  8) & 0xF) - 10 + 'A');
    buffer[8] = (((number >>  4) & 0xF) < 10) ? (((number >>  4) & 0xF) + '0') : (((number >>  4) & 0xF) - 10 + 'A');
    buffer[9] = (((number >>  0) & 0xF) < 10) ? (((number >>  0) & 0xF) + '0') : (((number >>  0) & 0xF) - 10 + 'A');
    buffer[10] = '\r';
    buffer[11] = '\n';
    buffer[12] = '\0';

    UART_send_buffer(buffer);
}
