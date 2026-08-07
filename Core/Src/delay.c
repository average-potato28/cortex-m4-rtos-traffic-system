#include "delay.h"
#include <stdint.h>
#include "stm32f4xx.h"
#include "cmsis_compiler.h"
void SystemClock_Config(void);
extern  int volatile bright[2];
extern uint32_t utick;
 void delay ( int itr){
	 for(volatile uint32_t i = 0; i < itr*1000; i++)
		 __NOP();
}




