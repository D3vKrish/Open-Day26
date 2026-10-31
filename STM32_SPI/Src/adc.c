#include "adc.h"
#include "stm32f446xx.h"
#include "gpio.h"


// Variable to store data
volatile uint32_t adc_val_phase1 = 0;
volatile uint32_t adc_val_phase2 = 0;
volatile uint32_t adc_val_phase3 = 0;

void ADC_Init(){
	GPIO_Init('A', 0, 3);
	GPIO_Init('A', 1, 3);
	GPIO_Init('A', 2, 3);


}

uint32_t ADC_GetValue(void){

}
