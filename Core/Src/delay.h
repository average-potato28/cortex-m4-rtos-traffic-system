#ifndef __delay_h__
#define __delay_h__
#include <stdint.h>
#include <stddef.h>

void main1 (void);
void SystemClock_Config(void);
void delay (int);

#define GPIO_C *((volatile uint32_t*) 0x40020818u)
#define LED_PC13_ALIAS *((volatile uint32_t*) 0x424102B4u)

int* swap (int* x, int* y);
void context_switch(void);

void SysTick_Handler(void);
void rtos_yield(void);

#ifndef NUMBER_OBJECTS
#define NUMBER_OBJECTS 16
#endif

#ifndef mx_thread
#define mx_thread 4
#endif

/* Increased stack_size from 100 to 256 to prevent snprintf stack overflow */
#define stack_size 256

typedef struct {
    uint32_t *sp;                    // First member (Offset 0)
    uint32_t timer;                  // Second member (Offset 4)
    uint32_t mem_alloc[stack_size];  // Last member (Offset 8)
} OS_boy;

extern uint32_t global_tick;
extern OS_boy Array[mx_thread];
extern OS_boy *arr[mx_thread];
extern OS_boy *curr;
extern OS_boy *prev;
extern volatile uint32_t SYS_i;
extern volatile uint32_t ready_reg[1];
extern int *address[mx_thread];

void PendSV_Handler(void);
void idle_thread(void);
int ready_flag(int index, int state);
extern volatile int uart_lock;

void pseudo_stack(int *ptr);
void SYS_ticks(void);
void SYS_prep(void);
void rtos_delay(uint32_t ticks);
void functions_init(void);

#endif
