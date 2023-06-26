#include <stdlib.h>
#include <threads.h>
#include <stdio.h>

// circular array
typedef struct _queue {
    int size;
    int used;
    int first;
    void **data;
    mtx_t mutex;
    cnd_t full; //avisa de que la cola ya no está llena (deja de estar llena)
    cnd_t empty; //avisa de que la cola ya no está vacía
    int desbloqueoRemove; //si estuviese a 1 nos despierta el remove y devuelve NULL
} _queue;

#include "queue.h"

queue q_create(int size) {
    queue q = malloc(sizeof(_queue));

    q->size  = size;
    q->used  = 0;
    q->first = 0;
    q->data  = malloc(size * sizeof(void *));
	q->desbloqueoRemove = 0;
    mtx_init(&q->mutex,mtx_plain);
    cnd_init(&q->full);
    cnd_init(&q->empty);

    return q;
}

//avisar a la cola de que si alguien está esperando 
//a que se inserte algún elemento en la cola
// deje de hacerlo, se despiertaria y devolveria NULL (remove)
void q_desbloqueo(queue q){
	mtx_lock(&q->mutex);
	q->desbloqueoRemove = 1;
	cnd_broadcast(&q->empty);
	mtx_unlock(&q->mutex);
}

int q_elements(queue q) {
    return q->used;;
}

int q_insert(queue q, void *elem) {
	
	int was_empty;
	mtx_lock(&q->mutex);
	//miramos si la cola esta llena:
	while(q->size==q->used) {
		cnd_wait(&q->full, &q->mutex); 
		//si la cola esta llena mandamos a dormir (espera)
		//hasta q se avise de que la cola ya no esta llena
	}
	//si se quedó esperando was_empty = 1 (true)
	//si no = 0 (false)
	was_empty = (q->used==0);
	q->data[(q->first+q->used) % q->size] = elem;
	q->used++;
	if(was_empty){
		//avisa de que la cola ya no está vacia
		//esto es util para la funcion remove:
		cnd_broadcast(&q->empty);
	}
	mtx_unlock(&q->mutex);
	return 0;
}

void *q_remove(queue q) {
	
	void *res;
	int was_full;
	
	mtx_lock(&q->mutex);

	//si estuviese q->desbloqueoRemove a 1 nos despierta el remove y devuelve NULL
	while(q->used==0 && !q->desbloqueoRemove){
		//si no hay elementos queda esperando
		cnd_wait(&q->empty, &q->mutex);
	}
	
	//no hay elementos y nunca va a haberlos, devolvemos NULL
	if(q->desbloqueoRemove && q->used==0 ){
		mtx_unlock(&q->mutex);
		return NULL;
	}
	
	//was_full = 1 (true) en cada de q estuviese llena
	was_full = (q->used==q->size);
    res=q->data[q->first];  
    q->first=(q->first+1) % q->size;
    q->used--;
	if(was_full){
		//si was_full true avisamos de que ya no esta llena
		//esto es util para insert
		cnd_broadcast(&q->full);
	}
	mtx_unlock(&q->mutex);
    
    return res;
}

void q_destroy(queue q) {
    free(q->data);
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->full);
    cnd_destroy(&q->empty);
    free(q);
}
