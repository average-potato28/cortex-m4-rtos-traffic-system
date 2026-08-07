#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "delay.h"
#include "cmsis_compiler.h"
#include <stdint.h>
#include "stm32f4xx.h"

/* In schedule_v4.c - Single point of definition */
OS_boy Array[mx_thread] __attribute__((aligned(8)));
OS_boy *arr[mx_thread];
OS_boy *curr = NULL;
OS_boy *prev = NULL;
volatile uint32_t SYS_i = 0;
volatile uint32_t ready_reg[(mx_thread + 31) / 32] = {0};
extern uint32_t SystemCoreClock;


void pseudo_stack(int *ptr) {
    int i = 1;

    arr[SYS_i]->mem_alloc[stack_size - i++] = 0x01000000;
    arr[SYS_i]->mem_alloc[stack_size - i++] = (uint32_t)ptr;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;

    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;
    arr[SYS_i]->mem_alloc[stack_size - i++] = 0;

    arr[SYS_i]->sp = &arr[SYS_i]->mem_alloc[stack_size - 16];

    arr[SYS_i]->timer = 0;

    ready_reg[SYS_i >> 5] |= (1UL << (SYS_i & 31));

    SYS_i= (SYS_i +1)%mx_thread;
}

void idle_thread(void) {
    while (1) {
        __asm volatile ("wfi");
    }
}
__attribute__((naked)) void SVC_Handler(void) {
    __asm volatile (
        "ldr r1, =curr          \n"
        "ldr r1, [r1]           \n"
        "ldr r0, [r1]           \n"

        "ldmia r0!, {r4-r11}    \n"
        "msr psp, r0            \n"

        "ldr lr, =0xFFFFFFFD    \n"
        "bx lr                  \n"
        ::: "memory"
    );
}

void SysTick_Handler(void) {
    SYS_ticks();
    SYS_prep();
}
int ready_flag(int index, int state) {int offset,bit;
offset = index/32;
bit= index%32;
    if (state == 0b01) { // 01 for Read State
        return (ready_reg[offset] & (1UL << bit)) ? 1 : 0;
    }
    else if (state == 0b11) { // 11 for Set Ready
        ready_reg[offset] |= (1UL << bit);
        return 0;
    }
    else if (state == 0b00) { // 00 for Reset/Clear
        ready_reg[offset] &= ~(1UL << bit);
        return 0;
    }

    return 0;
}
void SYS_ticks(void) {
    // Thread 0 (Idle Thread) is always ready. Only process user threads 1 to mx_thread-1.
    for (int t_i = 0; t_i < (mx_thread-1); t_i++) {
        if (arr[t_i]->timer > 0) {
            arr[t_i]->timer--;
            if (arr[t_i]->timer == 0) {
            	ready_reg[t_i >> 5] |= (1UL << (t_i & 31)); //  ready_flag(t_i, 0b11); // Set ready flag (binary 0b11) when delay expires
            }
        }
    }
}
void SYS_prep(void) {
    int curr_i = SYS_i; int found =0;
    prev = arr[SYS_i];

for (int i= 0;i<(mx_thread+31)/32;i++){
	if (ready_reg[i]!=0){
SYS_i = 32*i + __builtin_ctz(ready_reg[i]) ;
	found =1;
	break;}




}

    if (curr_i == SYS_i) {
        return;}

    curr = arr[SYS_i];
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Set PendSV bit
}

void rtos_delay(uint32_t ticks) {
    __disable_irq();
    if (ticks > 0) {
        arr[SYS_i]->timer = ticks;
        ready_reg[SYS_i >> 5] &= ~(1UL << (SYS_i & 31));
// Put thread to sleep (binary 0b00)
        SYS_prep();           // Schedule next active task
    }
    __enable_irq();
    __ISB();                  // Enforce immediate context switch
}

__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "mrs r0, psp            \n" // Read PSP
        "ldr r1, =prev          \n" // Load address of 'prev' pointer
        "ldr r1, [r1]           \n" // r1 = prev
      //  "cbz r1, skip_save      \n" // If prev is NULL (system startup), skip saving

        "stmdb r0!, {r4-r11}    \n" // Push software registers (r4-r11) onto task stack
        "str r0, [r1]           \n" // Save updated stack pointer to prev->sp (offset 0)

       // "skip_save:             \n"
        "ldr r1, =curr          \n" // Load address of 'curr' pointer
        "ldr r1, [r1]           \n" // r1 = curr
        "ldr r0, [r1]           \n" // r0 = curr->sp (offset 0)
        "ldmia r0!, {r4-r11}    \n" // Pop software registers from target stack
        "msr psp, r0            \n" // Load updated stack pointer to PSP

       // "ldr lr, =0xFFFFFFFD    \n" // Return using PSP
        "bx lr                  \n"
        ::: "memory"
    );
}










/*	while (1)
	{



		/*
	    HAL_Delay(500);
		LED_PC13_ALIAS ^= 1;}  */

// Define the global spinlock for the UART (0 = free, 1 = in use)
// Define the global spinlock for the UART (0 = free, 1 = in use)
volatile int uart_lock = 0;

// Yield function for immediate context switching without forced sleep
void rtos_yield(void) {
    __disable_irq();          // Protect global scheduler variables
    SYS_prep();               // Cue up the next ready thread
    __enable_irq();           // Open gates
    __ISB();                  // Enforce immediate context switch via PendSV
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    SCB->CPACR &= ~((3UL << 20) | (3UL << 22));//disable fpu
    for (int idx = 0; idx < mx_thread; idx++) {
        arr[idx] = &Array[idx];
    }

     functions_init( );


    SYS_i = 0;
    curr = arr[SYS_i];
    prev = NULL;

    SysTick->LOAD = (SystemCoreClock / 100U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = 0 ;

    HAL_NVIC_SetPriority(SysTick_IRQn, 15, 0);
    HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    *((volatile uint32_t*) 0x40020800u) &= ~(3<<26) ;
    *((volatile uint32_t*) 0x40020800u) |= (1<<26) ;









    __enable_irq();   SysTick->CTRL = 7 ; __asm volatile ("svc 0");
    while (1) {
    }
}
void SystemClock_Config(void)
{
	  LL_FLASH_SetLatency(LL_FLASH_LATENCY_3);
	  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_3)
	  {
	  }
	  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
	  LL_RCC_HSE_Enable();

	  while(LL_RCC_HSE_IsReady() != 1)
	  {

	  }
	  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_25, 200, LL_RCC_PLLP_DIV_2);
	  LL_RCC_PLL_Enable();

	  while(LL_RCC_PLL_IsReady() != 1)
	  {

	  }
	  while (LL_PWR_IsActiveFlag_VOS() == 0)
	  {
	  }
	  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
	  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
	  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
	  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

	  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
	  {

	  }
	  LL_SetSystemCoreClock(100000000);

	  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
	  {
	    Error_Handler();
	  }
	  LL_RCC_SetTIMPrescaler(LL_RCC_TIM_PRESCALER_TWICE);
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM11)
  {
    HAL_IncTick();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
