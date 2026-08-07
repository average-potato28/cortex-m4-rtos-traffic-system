
#include "delay.h"
#include "usart.h"







void functions_init( ){
	int* address[mx_thread] = { (int*)blue,(int*)green,
			(int*)red,(int*)yellow,(int*)white,(int*)idle_thread};
	for(int i=0;i<mx_thread;i++)
	 {pseudo_stack(address[i]);}

}
void blue(void) {
    while (1) {
        // Wait until UART is free, yielding the CPU to other threads
        while (uart_lock == 1) {
            rtos_yield();
        }

        uart_lock = 1; // Claim lock
        HAL_UART_Transmit(&huart1, (uint8_t*)"u00\r\n", 5, 10);
        uart_lock = 0; // Release lock

        // Sleep for 500ms (50 ticks @ 100Hz)
        rtos_delay(50);
    }
}

void green(void) {
    while (1) {
        while (uart_lock == 1) {
            rtos_yield();
        }

        uart_lock = 1;
        HAL_UART_Transmit(&huart1, (uint8_t*)"d11\r\n", 5, 10);
        uart_lock = 0;

        // Sleep for 300ms (30 ticks @ 100Hz)
        rtos_delay(30);
    }
}

void red(void) {
    while (1) {
        while (uart_lock == 1) {
            rtos_yield();
        }

        uart_lock = 1;
        HAL_UART_Transmit(&huart1, (uint8_t*)"i1\r\n", 4, 8);
        uart_lock = 0;

        // Sleep for 100ms (10 ticks @ 100Hz)
        rtos_delay(10);
    }
}

void yellow(void) {
    while (1) {
        while (uart_lock == 1) {
            rtos_yield();
        }

        uart_lock = 1;
        HAL_UART_Transmit(&huart1, (uint8_t*)"y0\r\n", 4, 8);
        GPIO_C = (1ul<<13);
        uart_lock = 0;

        // Sleep for 200ms (20 ticks @ 100Hz)
        rtos_delay(20);
    }
}

void white(void) {
    while (1) {
        // Sole control of the LED to prevent fighting threads


        while (uart_lock == 1) {
            rtos_yield();
        }

        uart_lock = 1;
        HAL_UART_Transmit(&huart1, (uint8_t*)"w0\r\n", 4, 8);
        GPIO_C = (1ul<<29);
        uart_lock = 0;

        // Sleep for 400ms (40 ticks @ 100Hz)
        rtos_delay(40);
    }
}
