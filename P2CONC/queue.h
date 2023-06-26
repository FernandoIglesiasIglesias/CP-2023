#ifndef __QUEUE_H__
#define __QUEUE_H__

typedef struct _queue *queue;

queue q_create(int size);            // Create a new queue
int   q_elements(queue q);             // number of elements in a queue

int   q_insert(queue q, void *elem); // insert an element into a queue
//q_insert(): bloquea mientras haya espacio para insertar

void *q_remove(queue q);             // remove an element from the queue
//q_remove(): bloquea mientras la cola esté vacía y no se active el desbloqueo

void  q_desbloqueo();
//q_desbloqueo: desactiva el bloqueo de q_remove() cuando la cola esté vacía

void  q_destroy(queue q);             // destroy a queue.

#endif
