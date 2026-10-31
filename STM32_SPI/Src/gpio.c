#include "gpio.h"
#include "stm32f446xx.h"
volatile uint32_t* GPIO_BASE;
volatile uint32_t* GPIO_MODER;
void GPIO_Init(char port, uint8_t pin, uint8_t mode){
	RCC->AHB1ENR |= (1 << (port-'A'));

	// Defining the registers from Base based on port
	GPIO_BASE = (volatile uint32_t*) (0x40020000 + (0x400 * (port-'A')));
	GPIO_MODER = (volatile uint32_t*) (GPIO_BASE + 0x00);

	*GPIO_MODER &= ~(3 << (2*pin));
	*GPIO_MODER |= (mode << (2*pin));

}
