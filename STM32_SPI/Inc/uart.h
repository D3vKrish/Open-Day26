#ifndef UART_H
#define UART_H
#include "stm32f4xx.h"

extern void UART_config(uint32_t baud);
extern void UART_send_buffer(const char* buffer);
extern void UART_send_uint32(uint32_t number);
extern void UART_send_hex8(uint8_t number);
extern void UART_send_hex16(uint16_t number);
extern void UART_send_hex32(uint32_t number);

#endif