
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


pool* pool_ctor (uint);

pool* pool_ctor (uint size){
	pool* pool_reg = (pool*)malloc(sizeof(pool));
	node** reg = (node**)malloc(sizeof(node*)*size);
	for (int i=0;i<size;i++)
	{ *(reg+i) = (node*)malloc(sizeof(node));

	}
	for (int i=0;i<size-1;i++)
		{ (*(reg+i))->next  = *(reg+(i+1));

		}

	 pool_reg->head = *reg ;
	 pool_reg->next = *(reg+1);
	 pool_reg->free = 1 ;

	 (*(reg+(size-1)))->next = NULL ;
	free(reg);
return pool_reg ;
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


node* pop_pool(pool* tool);
node* pop_pool(pool* tool){
	if (tool->free && (tool->head !=NULL)){
		tool->free = 0;
		node* temp= tool->head ;
			tool->head= tool->next;
	if (tool->next != NULL)
		tool->next = (tool->next)->next;
	tool->free = 1;	return temp;
	}
		else
			return NULL;
}









// --------------------------memory------------------pool--------------ends---------------------
typedef struct {


}event_reg;
typedef enum struct_id {
	master_struct_id,
	input_struct_id,
	output_struct_id
}struct_id;



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
	struct_id id;
	void* list[NUMBER_OBJECTS];
	int current_max;
}mail;
// -----------------------------queue------------manager-----------start----------------
typedef struct {
	uint push_i;
	uint pop_i;
	uint32_t bmask;
	node* buffer[NUMBER_OBJECTS];
	uint mx_buffer;
}fifo;
void push_queue(fifo*,node*);
void push_queue(fifo* me,node* ptr){
	me->buffer[me->push_i] = ptr ;
	me->bmask |= (1ul<<me->push_i);
	me->push_i = (me->push_i + 1)%me->mx_buffer ;
}
node* pop_queue (fifo* );
node* pop_queue (fifo* const me){
	if (me->bmask!=0)
	{
		node* temp=  me->buffer[me->pop_i];
		me->bmask &= ~(1ul<<me->pop_i);
		me->pop_i = (me->pop_i + 1)%me->mx_buffer;
		return temp;
	}
	else
		return NULL ;
}

//------------------------------queue-------------manager------------end-------------------


// ------------------observer----------pattern--------------starts----------------

bool subscribe_master(master* ,void* );
bool subscribe_master(master* me, void* subscriber){
	me->_mail.list[me->_mail.current_max] =subscriber;
	++me->_mail.current_max;
	return 1;

}
typedef struct {


}publisher;
bool publish (void* _me,node*);
bool publish (void* _me,node* data){
	if (_me == NULL) {
	        return false;
	    }

	  mail* me = (mail*)_me;

	for (int i=0;i<(mail*)(me)->current_max;++i)
	{
	switch (*((struct_id*)(me->list[i]))) {
	case master_struct_id :
		*(((master*)(me->list[i]))->vptr->on_notify)((fifo*)&((master*)(me->list[i]))->queue,data);
		break;
	case input_struct_id:
				*(((input*)(me->list[i]))->vptr->on_notify)((fifo*)&((input*)(me->list[i]))->queue,data);
		break;
	case output_struct_id:
						*(((output*)(me->list[i]))->vptr->on_notify)((fifo*)&((output*)(me->list[i]))->queue,data);
		break;
	     }
	}
}










//-------------------------observer----------------pattern-----------ends--------------






// -------------------------------OUTPUT--------SECTION--------------START-------------

output* output_handler_ctor();


output* output_handler_ctor() {
	static output temp;
	static out_vtable table;


	table = (out_vtable){ .on_notify = push_queue, .send_uart = send, .read_queue= pop_queue };


	temp = (output){
		 ._mail = { .id = output_struct_id  ,.current_max = 0 },
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

typedef struct {
	void (*on_notify)(fifo* const me, node* data);
	void (*send_uart)(output* const me);
	void (*read_queue)(output* const me);
} out_vtable;


typedef struct {
	mail _mail;
out_vtable* vptr;
fifo queue;
uint8_t south;
uint8_t north;
uint8_t east;
uint8_t west;
}output;

//------------------------------------OUTPUT-----------SECTION---------------END-------------------


//-------------------------------------master-----------section---------------starts------------------

typedef struct {
	mail _mail;
	master_vtable* vptr;

fifo queue;

}master;

typedef struct {
	void (*on_notify)(fifo* const me, node* data);
	void (*pop_master)(output* const me);
	void (*stuff)(output* const me);
} master_vtable;


void master_handler (master* const me);
 master_handler (master* const me){

}
 master* master_handler_ctor();


 master* master_handler_ctor() {
 	static master temp;
 	static master_vtable table;


 	table = (master_vtable){ .on_notify = push_queue, .pop_master = pop_queue, .stuff= master_handler };


 	temp = (master){
		 ._mail = { .id = master_struct_id ,.current_max = 0 },
         .vptr = &table,
         .queue = { .push_i = 0, .pop_i = 0, .bmask = 0, .mx_buffer= NUMBER_OBJECTS }

     };

     return &temp;
 }












//-------------------------------------master---------------section----------------ends------------------------
typedef struct {

};

typedef struct {

};


typedef struct {

};

