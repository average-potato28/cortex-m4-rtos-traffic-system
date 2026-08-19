#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "stm32f4xx.h"
#include "usart.h"
#include "delay.h"

#ifndef NUMBER_OBJECTS
#define NUMBER_OBJECTS 16
#endif

#ifndef mx_thread
#define mx_thread 4
#endif

//90.0 seconds at 100Hz SysTick (1 tick = 10ms)
#define MAX_WAIT_TICKS 9000ul


//----------------------DATA STRUCTURES & PAYLOADS---------

typedef enum {
    MSG_EMERGENCY,
    MSG_EMERGENCY_CLEARED,
    MSG_QUEUE_UPDATE
} input_msg_type_t;

typedef struct {
    input_msg_type_t type;
    uint8_t direction;  //0: Right ('r'), 1: Down ('d'), 2: Left ('l'), 3: Up ('u')
    uint8_t queue_len;  //Queue length in meters (0 - 50m)
} input_payload;

typedef struct {
    uint8_t direction;  //0: Right, 1: Down, 2: Left, 3: Up
    uint8_t state;      //2: Green ("11"), 1: Yellow ("01"), 0: Red ("00")
} master_payload;

typedef struct {
    enum {
        input_id,
        master_id
    } id;
    union {
        input_payload input;
        master_payload master;
    } data;
} universal_payload;

//-------------------memory pool definition  WARNING CRITICAL SECTION AHEAD  WARNING -----------------------
typedef struct node {
    union {
        struct node* next;
        universal_payload message;
    };
} node;

typedef struct {
    node* head;
} pool;

static pool* global_pool = NULL;

pool* pool_ctor(uint32_t size) {
    pool* pool_reg = (pool*)malloc(sizeof(pool));
    node* first = NULL;
    node* current = NULL;

    for (uint32_t i = 0; i < size; i++) {
        node* n = (node*)malloc(sizeof(node));
        n->next = NULL;
        if (first == NULL) {
            first = n;
            current = n;
        } else {
            current->next = n;
            current = n;
        }
    }

    pool_reg->head = first;
    return pool_reg;
}

bool push_pool(node* ptr, pool* tool) {
    if (tool == NULL || ptr == NULL) return false;

    __disable_irq();
    ptr->next = tool->head;
    tool->head = ptr;
    __enable_irq();

    return true;
}

node* pop_pool(pool* tool) {
    if (tool == NULL) return NULL;
    node* temp = NULL;

    __disable_irq();
    temp = tool->head;
    if (temp != NULL) {
        tool->head = temp->next;
    }
    __enable_irq();

    return temp;
}

//---------------------MAIL & FIFO QUEUE MANAGEMENT=------------------------
typedef enum struct_id {
    master_struct_id,
    input_struct_id,
    output_struct_id
} struct_id;

typedef struct {
    struct_id id;
    void* list[NUMBER_OBJECTS];
    int current_max;
} mail;

typedef struct {
    volatile uint32_t push_i;
    volatile uint32_t pop_i;
    volatile uint32_t bmask;
    node* buffer[NUMBER_OBJECTS];
    uint32_t mx_buffer;
} fifo;

bool push_queue(fifo* me, node* ptr) {
    __disable_irq();
    if (me->bmask & (1ul << me->push_i)) {
        __enable_irq();
        return false;
    }
    me->buffer[me->push_i] = ptr;
    me->bmask |= (1ul << me->push_i);
    me->push_i = (me->push_i + 1) % me->mx_buffer;
    __enable_irq();
    return true;
}

node* pop_queue(fifo* const me) {
    node* temp = NULL;
    __disable_irq();
    if (me->bmask != 0) {
        temp = me->buffer[me->pop_i];
        me->bmask &= ~(1ul << me->pop_i);
        me->pop_i = (me->pop_i + 1) % me->mx_buffer;
    }
    __enable_irq();
    return temp;
}

//--------------------FINITE STATE MACHINE(master) ------------------
typedef enum {
    STATE_GREEN,
    STATE_YELLOW,
    STATE_EMERGENCY_YELLOW,
    STATE_EMERGENCY_GREEN
} master_state_t;

//--------------------------HANDLERS & VTABLES------------------------------
typedef struct {
    char    cmd_type;   //'E', 'C', 'R', 'D', 'L', 'U', or 0 (idle)
    char    num_buf[3]; //up to 2 digits + NUL
    uint8_t num_idx;
} input_parse_state;

typedef struct input_vtable input_vtable;
typedef struct {
    mail _mail;
    input_vtable* vptr;
    fifo queue;
    input_parse_state parse;
} input;

struct input_vtable {
    bool (*on_notify)(fifo* const me, node* data);
    void (*read_uart)(input* const me);
};

typedef struct master_vtable master_vtable;
typedef struct {
    mail _mail;
    master_vtable* vptr;
    fifo queue;
    uint8_t current_green;
    uint8_t queue_lengths[4];
    uint32_t last_green_tick[4];      //Tracks when each lane last received green
    bool emergency_active;
    uint8_t emergency_dir;
    uint8_t interrupted_dir;          //Remembers the preempted lane
    bool emergency_cleared_signal;    //Signal EV has crossed the stop line
    master_state_t state;
} master;

struct master_vtable {
    bool (*on_notify)(fifo* const me, node* data);
    void (*pop_master)(void* me);
    void (*stuff)(void* me);
};

typedef struct out_vtable out_vtable;
typedef struct {
    mail _mail;
    out_vtable* vptr;
    fifo queue;
    uint8_t south;
    uint8_t north;
    uint8_t east;
    uint8_t west;
} output;

struct out_vtable {
    bool (*on_notify)(fifo* const me, node* data);
    void (*send_uart)(output* const me);
    void (*read_queue)(output* const me);
};

input* g_input = NULL;
master* g_master = NULL;
output* g_output = NULL;

//-------------OBSERVER PATTERN IMPLEMENT------------- all my homies hate publish subsccribe too much abstraction , not needed here
bool subscribe_master(master* me, void* subscriber) {
    if (me == NULL || subscriber == NULL) return false;
    if (me->_mail.current_max >= NUMBER_OBJECTS) return false;
    me->_mail.list[me->_mail.current_max] = subscriber;
    ++me->_mail.current_max;
    return true;
}

bool subscribe_input(input* me, void* subscriber) {
    if (me == NULL || subscriber == NULL) return false;
    if (me->_mail.current_max >= NUMBER_OBJECTS) return false;
    me->_mail.list[me->_mail.current_max] = subscriber;
    ++me->_mail.current_max;
    return true;
}

bool publish(void* _me, node* data) {
    if (_me == NULL || data == NULL) return false;

    mail* me = (mail*)_me;

    for (int i = 0; i < me->current_max; ++i) {
        switch (*((struct_id*)(me->list[i]))) {
            case master_struct_id:
                ((master*)(me->list[i]))->vptr->on_notify((fifo*)&((master*)(me->list[i]))->queue, data);
                break;
            case input_struct_id:
                ((input*)(me->list[i]))->vptr->on_notify((fifo*)&((input*)(me->list[i]))->queue, data);
                break;
            case output_struct_id:
                ((output*)(me->list[i]))->vptr->on_notify((fifo*)&((output*)(me->list[i]))->queue, data);
                break;
        }
    }
    return true;
}

//---------------------------------UART POTATO------------------------
static void uart_send_str(const char* str) {
    while (*str) {
        while (!(USART1->SR & USART_SR_TXE));
        USART1->DR = (uint32_t)(*str++);
    }
}

//-----------INPUT SECTION (checks for errors and saves current state in .data section static initialized )
input* input_handler_ctor(void) {
    static input temp;
    static input_vtable table;

    table = (input_vtable){ .on_notify = push_queue, .read_uart = NULL };

    temp = (input){
        ._mail = { .id = input_struct_id, .current_max = 0 },
        .vptr = &table,
        .queue = { .push_i = 0, .pop_i = 0, .bmask = 0, .mx_buffer = NUMBER_OBJECTS }
    };

    return &temp;
}

static bool dir_from_char(char c, uint8_t* out_dir) {
    switch (c) {
        case 'r': case 'R': *out_dir = 0; return true;
        case 'd': case 'D': *out_dir = 1; return true;
        case 'l': case 'L': *out_dir = 2; return true;
        case 'u': case 'U': *out_dir = 3; return true;
        default: return false;
    }
}

static void emit_input_message(input* const me, input_msg_type_t type, uint8_t dir, uint8_t q_len) {
    node* n = pop_pool(global_pool);
    if (n == NULL) return;
    n->message.id = input_id;
    n->message.data.input.type = type;
    n->message.data.input.direction = dir;
    if (type == MSG_QUEUE_UPDATE) {
        n->message.data.input.queue_len = q_len;
    }
    publish((void*)&me->_mail, n);
}

void input_handler(input* const me) {
    input_parse_state* ps = &me->parse;

    while (USART1->SR & USART_SR_RXNE) {
        uint32_t sr = USART1->SR;
        if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
            volatile uint32_t dummy = USART1->DR;
            (void)dummy;
            continue;
        }

        char ch = (char)(USART1->DR & 0xFF);

        if (ch == 'E' || ch == 'C') {
            ps->cmd_type = ch;
            ps->num_idx = 0;
        } else if (ch == 'R' || ch == 'D' || ch == 'L' || ch == 'U') {
            ps->cmd_type = ch;
            ps->num_idx = 0;
        } else if (ps->cmd_type == 'E' || ps->cmd_type == 'C') {
            uint8_t dir = 0;
            if (dir_from_char(ch, &dir)) {
                input_msg_type_t msg_type = (ps->cmd_type == 'E') ? MSG_EMERGENCY : MSG_EMERGENCY_CLEARED;
                emit_input_message(me, msg_type, dir, 0);
            }
            ps->cmd_type = 0;
        } else if (ps->cmd_type == 'R' || ps->cmd_type == 'D' || ps->cmd_type == 'L' || ps->cmd_type == 'U') {
            if (ch >= '0' && ch <= '9') {
                if (ps->num_idx < 2) {
                    ps->num_buf[ps->num_idx++] = ch;
                }
                if (ps->num_idx == 2) {
                    ps->num_buf[2] = '\0';
                    uint8_t dir = 0;
                    dir_from_char(ps->cmd_type, &dir);
                    uint8_t q_len = (uint8_t)atoi(ps->num_buf);
                    emit_input_message(me, MSG_QUEUE_UPDATE, dir, q_len);
                    ps->cmd_type = 0;
                    ps->num_idx = 0;
                }
            } else if (ch == '\n' || ch == '\r') {
                if (ps->num_idx > 0) {
                    ps->num_buf[ps->num_idx] = '\0';
                    uint8_t dir = 0;
                    dir_from_char(ps->cmd_type, &dir);
                    uint8_t q_len = (uint8_t)atoi(ps->num_buf);
                    emit_input_message(me, MSG_QUEUE_UPDATE, dir, q_len);
                }
                ps->cmd_type = 0;
                ps->num_idx = 0;
            }
        }
    }
}

//-------------------------- MASTER SECTION (Priority Queue, 90s Starvation & Emergency Engine)------------
static uint8_t select_highest_queue_direction(
    uint8_t current,
    const uint8_t queue_lengths[4],
    const uint32_t last_green_tick[4],
    uint32_t current_tick,
    bool emergency_active,
    uint8_t emergency_dir)
{
    //1. Emergency vehicle supercedes all
    if (emergency_active) {
        return emergency_dir;
    }

    //2. Anti-Starvation Check 90 section lilmit
    uint8_t starved_dir = 255;
    uint32_t max_starve_wait = 0;

    for (uint8_t offset = 1; offset <= 3; offset++) {
        uint8_t i = (current + offset) % 4;
        uint32_t wait_ticks = current_tick - last_green_tick[i];

        if (wait_ticks >= MAX_WAIT_TICKS) {
            if (wait_ticks > max_starve_wait) {
                max_starve_wait = wait_ticks;
                starved_dir = i;
            }
        }
    }

    //If any lane has waited >= 90s, give it green asap
    if (starved_dir != 255) {
        return starved_dir;
    }

    //3. Proportional Queue Priority Evaluation
    uint8_t best_dir = (current + 1) % 4;
    uint8_t max_q = queue_lengths[best_dir];

    for (uint8_t offset = 2; offset <= 3; offset++) {
        uint8_t i = (current + offset) % 4;
        if (queue_lengths[i] > max_q) {
            max_q = queue_lengths[i];
            best_dir = i;
        }
    }

    if (max_q == 0 && queue_lengths[current] == 0) {
        return (current + 1) % 4;
    }

    if (queue_lengths[current] > max_q) {
        return current;
    }

    return best_dir;
}

master* master_handler_ctor(void) {
    static master temp;
    static master_vtable table;

    table = (master_vtable){ .on_notify = push_queue, .pop_master = NULL, .stuff = NULL };

    temp = (master){
        ._mail = { .id = master_struct_id, .current_max = 0 },
        .vptr = &table,
        .queue = { .push_i = 0, .pop_i = 0, .bmask = 0, .mx_buffer = NUMBER_OBJECTS },
        .current_green = 0,
        .queue_lengths = {0, 0, 0, 0},
        .last_green_tick = {0, 0, 0, 0},
        .emergency_active = false,
        .emergency_dir = 0,
        .interrupted_dir = 255,
        .emergency_cleared_signal = false,
        .state = STATE_GREEN
    };

    return &temp;
}

void master_handler(master* const me) {
    static uint32_t state_start_tick = 0;
    static uint32_t green_duration_ticks = 150; //1.5second base green duration
    static bool initial_boot = true;

    if (initial_boot) {
        initial_boot = false;
        state_start_tick = global_tick;

        //Initialize last green timestamp for all lanes
        for (uint8_t i = 0; i < 4; i++) {
            me->last_green_tick[i] = global_tick;
        }

        node* cmd = pop_pool(global_pool);
        if (cmd != NULL) {
            cmd->message.id = master_id;
            cmd->message.data.master.direction = me->current_green;
            cmd->message.data.master.state = 2; // Green
            publish((void*)&me->_mail, cmd);
        }
    }

    //1Process queued messages from Input Observer
    node* in_node = pop_queue(&me->queue);
    while (in_node != NULL) {
        if (in_node->message.id == input_id) {
            if (in_node->message.data.input.type == MSG_EMERGENCY) {
                me->emergency_active = true;
                me->emergency_dir = in_node->message.data.input.direction;
                me->emergency_cleared_signal = false;
            } else if (in_node->message.data.input.type == MSG_EMERGENCY_CLEARED) {
                if (me->emergency_dir == in_node->message.data.input.direction) {
                    me->emergency_cleared_signal = true;
                }
            } else if (in_node->message.data.input.type == MSG_QUEUE_UPDATE) {
                uint8_t d = in_node->message.data.input.direction;
                if (d < 4) me->queue_lengths[d] = in_node->message.data.input.queue_len;
            }
        }
        push_pool(in_node, global_pool);
        in_node = pop_queue(&me->queue);
    }

    //2 Emergency Preemption (Strictly supersedes 90s starvation)
    if (me->emergency_active) {
        if (me->current_green == me->emergency_dir && me->state == STATE_EMERGENCY_GREEN) {
            state_start_tick = global_tick;
            me->emergency_active = false;
            me->emergency_cleared_signal = false;
            me->last_green_tick[me->current_green] = global_tick;
        } else if (me->current_green == me->emergency_dir && me->state == STATE_GREEN) {
            me->interrupted_dir = me->current_green;
            me->state = STATE_EMERGENCY_GREEN;
            state_start_tick = global_tick;
            me->emergency_active = false;
            me->emergency_cleared_signal = false;
            me->last_green_tick[me->current_green] = global_tick;

            node* cmd = pop_pool(global_pool);
            if (cmd != NULL) {
                cmd->message.id = master_id;
                cmd->message.data.master.direction = me->current_green;
                cmd->message.data.master.state = 2;
                publish((void*)&me->_mail, cmd);
            }
        } else {
            if (me->state != STATE_EMERGENCY_YELLOW) {
                me->interrupted_dir = me->current_green; //Remember lane to return to
                me->state = STATE_EMERGENCY_YELLOW;
                state_start_tick = global_tick;
                me->emergency_cleared_signal = false;

                node* cmd = pop_pool(global_pool);
                if (cmd != NULL) {
                    cmd->message.id = master_id;
                    cmd->message.data.master.direction = me->current_green;
                    cmd->message.data.master.state = 1; //Yellow
                    publish((void*)&me->_mail, cmd);
                }
            }
        }
    }

    //3 FSM State Transitions
    uint32_t elapsed_ticks = global_tick - state_start_tick;

    switch (me->state) {
        case STATE_GREEN: {
            if (elapsed_ticks >= green_duration_ticks) {
                uint8_t next_priority_dir = select_highest_queue_direction(
                    me->current_green,
                    me->queue_lengths,
                    me->last_green_tick,
                    global_tick,
                    me->emergency_active,
                    me->emergency_dir);

                if (next_priority_dir == me->current_green && !me->emergency_active && me->queue_lengths[me->current_green] > 0) {
                    state_start_tick = global_tick;
                    me->last_green_tick[me->current_green] = global_tick;

                    uint8_t q_len = me->queue_lengths[me->current_green];
                    if (q_len > 50) q_len = 50;
                    //Proportional formula: 1.5s (150 ticks) to 6.0s (600 ticks)
                    green_duration_ticks = 150 + ((uint32_t)q_len * 450) / 50;
                } else {
                    me->state = STATE_YELLOW;
                    state_start_tick = global_tick;

                    node* cmd = pop_pool(global_pool);
                    if (cmd != NULL) {
                        cmd->message.id = master_id;
                        cmd->message.data.master.direction = me->current_green;
                        cmd->message.data.master.state = 1; //Yellow
                        publish((void*)&me->_mail, cmd);
                    }
                }
            }
            break;
        }

        case STATE_YELLOW: {
            if (elapsed_ticks >= 150) { //1.5s Yellow
                node* cmd = pop_pool(global_pool);
                if (cmd == NULL) {
                    break;
                }
                cmd->message.id = master_id;
                cmd->message.data.master.direction = me->current_green;
                cmd->message.data.master.state = 0; // Red
                publish((void*)&me->_mail, cmd);

                //Priority Selection, Starvation or Return Phase
                if (me->emergency_active) {
                    me->current_green = me->emergency_dir;
                    me->state = STATE_EMERGENCY_GREEN;
                    me->emergency_active = false;
                    me->emergency_cleared_signal = false;
                } else if (me->interrupted_dir < 4) {
                    //Turn back to the interrupted lane
                    me->current_green = me->interrupted_dir;
                    me->interrupted_dir = 255; //Reset return flag
                    me->state = STATE_GREEN;

                    uint8_t q_len = me->queue_lengths[me->current_green];
                    if (q_len > 50) q_len = 50;
                    green_duration_ticks = 150 + ((uint32_t)q_len * 450) / 50;
                } else {
                    me->current_green = select_highest_queue_direction(
                        me->current_green,
                        me->queue_lengths,
                        me->last_green_tick,
                        global_tick,
                        me->emergency_active,
                        me->emergency_dir);

                    me->state = STATE_GREEN;

                    uint8_t q_len = me->queue_lengths[me->current_green];
                    if (q_len > 50) q_len = 50;
                    green_duration_ticks = 150 + ((uint32_t)q_len * 450) / 50;
                }

                state_start_tick = global_tick;
                me->last_green_tick[me->current_green] = global_tick; //Reset starvation timer for active lane

                cmd = pop_pool(global_pool);
                if (cmd != NULL) {
                    cmd->message.id = master_id;
                    cmd->message.data.master.direction = me->current_green;
                    cmd->message.data.master.state = 2; //Green
                    publish((void*)&me->_mail, cmd);
                }
            }
            break;
        }

        case STATE_EMERGENCY_YELLOW: {
            if (elapsed_ticks >= 150) {
                node* cmd = pop_pool(global_pool);
                if (cmd == NULL) {
                    break;
                }
                cmd->message.id = master_id;
                cmd->message.data.master.direction = me->current_green;
                cmd->message.data.master.state = 0;
                publish((void*)&me->_mail, cmd);

                me->current_green = me->emergency_dir;
                me->state = STATE_EMERGENCY_GREEN;
                state_start_tick = global_tick;
                me->emergency_active = false;
                me->emergency_cleared_signal = false;
                me->last_green_tick[me->current_green] = global_tick;

                cmd = pop_pool(global_pool);
                if (cmd != NULL) {
                    cmd->message.id = master_id;
                    cmd->message.data.master.direction = me->current_green;
                    cmd->message.data.master.state = 2;
                    publish((void*)&me->_mail, cmd);
                }
            }
            break;
        }

        case STATE_EMERGENCY_GREEN: {
            //Terminate as soon as the EV clear
            bool ev_cleared = (me->emergency_cleared_signal && elapsed_ticks >= 100);
            bool ev_timeout = (elapsed_ticks >= 1500);

            if (ev_cleared || ev_timeout || me->emergency_active) {
                me->state = STATE_YELLOW;
                state_start_tick = global_tick;

                node* cmd = pop_pool(global_pool);
                if (cmd != NULL) {
                    cmd->message.id = master_id;
                    cmd->message.data.master.direction = me->current_green;
                    cmd->message.data.master.state = 1; //Yellow
                    publish((void*)&me->_mail, cmd);
                }
            }
            break;
        }
    }
}

//---------------------------------OUTPUT SECTION--------------------------------
output* output_handler_ctor(void) {
    static output temp;
    static out_vtable table;

    table = (out_vtable){ .on_notify = push_queue, .send_uart = NULL, .read_queue = NULL };

    temp = (output){
        ._mail = { .id = output_struct_id, .current_max = 0 },
        .vptr = &table,
        .queue = { .push_i = 0, .pop_i = 0, .bmask = 0, .mx_buffer = NUMBER_OBJECTS },
        .south = 0, .north = 0, .west = 0, .east = 0
    };

    return &temp;
}

void output_handler(output* const me) {
    static uint32_t heartbeat_counter = 0;
    static const char dir_map[4] = {'r', 'd', 'l', 'u'};

    if (++heartbeat_counter >= 10) {
        uart_send_str("i1\n");
        heartbeat_counter = 0;
    }

    node* cmd_node = pop_queue(&me->queue);
    while (cmd_node != NULL) {
        if (cmd_node->message.id == master_id) {
            uint8_t dir = cmd_node->message.data.master.direction;
            uint8_t state = cmd_node->message.data.master.state;

            char tx_buf[8];
            const char* state_str = (state == 2) ? "11" : ((state == 1) ? "01" : "00");
            snprintf(tx_buf, sizeof(tx_buf), "%c%s\n", dir_map[dir % 4], state_str);

            uart_send_str(tx_buf);

            switch (dir % 4) {
                case 0: me->east  = state; break;
                case 1: me->south = state; break;
                case 2: me->west  = state; break;
                case 3: me->north = state; break;
            }
        }
        push_pool(cmd_node, global_pool);
        cmd_node = pop_queue(&me->queue);
    }
}

//-----------------------------RTOS THREAD INITIALIZATION------------------
void input_thread(void) {
    while (1) {
        input_handler(g_input);
        rtos_yield();
    }
}

void master_thread(void) {
    while (1) {
        master_handler(g_master);
        rtos_delay(2);
    }
}

void output_thread(void) {
    while (1) {
        output_handler(g_output);
        rtos_delay(2);
    }
}

void functions_init(void) {
    global_pool = pool_ctor(NUMBER_OBJECTS);

    g_input = input_handler_ctor();
    g_master = master_handler_ctor();
    g_output = output_handler_ctor();

    subscribe_input(g_input, g_master);
    subscribe_master(g_master, g_output);

    int* thread_addrs[mx_thread] = {
        (int*)master_thread, // Priority 0
        (int*)output_thread, // Priority 1
        (int*)input_thread,  // Priority 2
        (int*)idle_thread    // Priority 3
    };// the way i bhave mapped priority in my real time kernel order of initialization decides priority , bcs its simple  maybe gotta change thatv in future

    for (int i = 0; i < mx_thread; i++) {
        pseudo_stack(thread_addrs[i]);
    }
}
