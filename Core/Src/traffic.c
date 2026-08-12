
#include "delay.h"
#include "usart.h"







void functions_init( ){
	int* address[mx_thread] = {
			(int*)blue,// threads to run
			(int*)green,
			(int*)red,
			(int*)yellow,
			(int*)white,
			(int*)idle_thread};
	for(int i=0;i<mx_thread;i++)
	 {pseudo_stack(address[i]);}

}




// ---------------------memory-----------------pool----------------start ------------------
typedef struct node {
	union {
		struct node* next ;
		universal_payload message ;
	};
}node;
typedef struct {
	node* head;
	node* next ;
	bool free;
}pool;


void pool_ctor (uint);

void pool_ctor (uint size){
	static pool pool_reg;
	node** reg = (node**)malloc(sizeof(node*)*size);
	for (int i=0;i<size;i++)
	{ *(reg+i) = (node*)malloc(sizeof(node));

	}
	for (int i=0;i<size-1;i++)
		{ (*(reg+i))->next  = *(reg+(i+1));

		}
	 pool_reg.head = *reg ;
	 (*(reg+(size-1)))->next = NULL ;
	free(reg);

}
bool push_pool(node*, pool*);
bool push_pool(node* ptr,pool* tool){
	if (tool->free){
	tool->free =0;

	 tool->next = tool->head ;
		 ptr->next = tool->head;
	 tool->head = ptr;
	tool->free=1; return 1;}
	else
		return 0;
}


node* pop_pool(void,pool* tool);
node* pop_pool(void,pool* tool){
	if (tool->free){
		node* temp= tool->head ;
			tool->head= tool->next;
		tool->next = (tool->next)->next;
		return temp;
	}
		else
			return (node*)(000000);
			return 0;
}









// --------------------------memory------------------pool--------------ends---------------------
typedef struct {


}event_reg;


typedef struct {
	 enum {
			input_id,
			master_id,
			}id;
			union {
		input_payload input;
		master_payload master;
	}data;
}universal_payload;


typedef struct {

}input_payload;

typedef struct {

}master_payload;


typedef struct {
	void* list[NUMBER_OBJECTS][1];
	int current_max;
}mail;

typedef struct {
	uint push_i;
	uint pop_i;
	uint32_t bmask;
	node* buffer[NUMBER_OBJECTS];
	uint mx_buffer;
}fifo;
// -------------------------------OUTPUT--------SECTION--------------START-------------

output* output_handler_ctor();


output* output_handler_ctor() {
	static output temp;
	static out_vtable table;


	table = (out_vtable){ .on_notify = push_out, .send_uart = send, .read_queue= pop_out };


	temp = (output){
        .vptr = &table,
        .queue = { .push_i = 0, .pop_i = 0, .bmask = 0, .mx_buffer= NUMBER_OBJECTS },
        .south = 00,
        .north = 00,
        .west = 00,
        .east = 00
    };

    return &temp;
}
void output_handler (output* const me);
 output_handler (output* const me){

}
 void pop_out(output*);
 void pop_out(output*me){


 }
void push_out(output*,node*);
void push_out(output*me,node* ptr){
	me->queue.buffer[me->queue.push_i] = ptr ;
	me->queue.push_i = (me->queue.push_i + 1)%me->queue.mx_buffer ;

}
void send(output*);
void send(output*){

}
typedef struct {
	void (*on_notify)(output* const me, node* data);
	void (*send_uart)(output* const me);
	void (*read_queue)(output* const me);
} out_vtable;


typedef struct {
out_vtable* vptr;
fifo queue;
uint8_t south;
uint8_t north;
uint8_t east;
uint8_t west;
}output;

//------------------------------------OUTPUT-----------SECTION---------------END-------------------
typedef struct {

};

typedef struct {

};


typedef struct {

};

