#include <errno.h>
#include <threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "options.h"

#define DELAY_SCALE 1000

struct array {
    int    size;
    int    *arr;
    mtx_t* mutex; //mutex
};

struct args {
    mtx_t        *mutex;    //mutex for the iterations
    int          thread_num;  // application defined thread #
    int          delay;      // delay between ops
    struct iter  *iter; // number of operations
    struct array *array; //pointer to the array
};

struct thread_info {
    pthread_t    id;    //id returned by pthread_create()
    struct args *args;  //pointer to the arguments
};

struct iter {
    int iterador;
    mtx_t mutex;
};

void apply_delay(int delay) {
    for(int i = 0; i < delay * DELAY_SCALE; i++); // waste time
}

int increment(int id, struct iter *iter,int delay, struct array *arr,mtx_t* mutex)
{
    int pos, val;

    mtx_lock(&iter->mutex);
    while(iter->iterador) {
        iter->iterador--;
        mtx_unlock(&iter->mutex);
        
        pos = rand() % arr->size;

        printf("%d increasing position %d\n", id, pos);
        
        mtx_lock(&mutex[pos]);
        val = arr->arr[pos];
        apply_delay(delay);

        val ++;
        apply_delay(delay);

        arr->arr[pos] = val;
        apply_delay(delay);
        mtx_unlock(&mutex[pos]);
        mtx_lock(&iter->mutex);
    }
    mtx_unlock(&iter->mutex);
    return 0;
}

int *increment2(void *ptr) {
    struct args *args = ptr;
    increment(args->thread_num,args->iter,args->delay,args->array,args->array->mutex);
    return 0;
}

/*
int swap(int id, int iterations, int delay, struct array *arr,mtx_t* mutex)
{
    int pos1, pos2, val;
    for(int i = 0; i < iterations; i++) {
        pos1 = rand() % arr->size;
        pos2 = rand() % arr->size;
        while(1) {
            if(!mtx_trylock(&mutex[pos1])) {
                if(!mtx_trylock(&mutex[pos2])) {
                    break;
                }   
                mtx_unlock(&mutex[pos2]);
                usleep(delay);
            }
        }
        val = arr->arr[pos1];
        apply_delay(delay);
        val--;
        apply_delay(delay);
        arr->arr[pos1] = val;
        apply_delay(delay);
        printf("Swap: %d position %d (-1), val = %d\n", id, pos1, val);
        val = arr->arr[pos2];
        apply_delay(delay);
        val++;
        apply_delay(delay);
        arr->arr[pos2] = val;
        apply_delay(delay);
        printf("Swap: %d position %d (+1), val = %d\n", id, pos2, val);
    }
    return 0;
}
*/

int swap(int id, struct iter *iter, int delay, struct array *arr,mtx_t* mutex) {
    int pos1, pos2, val1, val2;

    mtx_lock(&iter->mutex);
    while(iter->iterador) {

        iter->iterador--;
        mtx_unlock(&iter->mutex);

        pos1 = rand() % arr->size;
        pos2 = rand() % arr->size;

        do {
            pos2 = rand() % arr->size;
        } while(pos1==pos2);

        printf("%d moving %d and %d\n",id,pos1,pos2);

        mtx_lock(&mutex[pos1]);
        val1 = arr->arr[pos1];
        apply_delay(delay);

        val1--;
        apply_delay(delay);

        arr->arr[pos1] = val1;
        apply_delay(delay);

        mtx_unlock(&mutex[pos1]);

        mtx_lock(&mutex[pos2]);
        val2 = arr->arr[pos2];
        apply_delay(delay);

        val2++;
        apply_delay(delay);

        arr->arr[pos2] = val2;
        apply_delay(delay);


        mtx_unlock(&mutex[pos2]);

        mtx_lock(&iter->mutex);
    }
    mtx_unlock(&iter->mutex);
    return 0;
}

int *swap2(void *ptr) {
    struct args *args = ptr;
    swap(args->thread_num, args->iter ,args->delay,args->array,args->array->mutex);
    return 0;
}

struct thread_info *startThreads(struct options opt, struct array *arr,void *(tipo), struct iter *iter)
{
    int i;
    //int *iterations;
    
    struct thread_info *threads;
    mtx_t *mutex;

    threads = malloc(sizeof(struct thread_info) * opt.num_threads);

    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    mutex = malloc(sizeof(mtx_t));
    //iterations = malloc(sizeof(int));
    mtx_init(mutex, 0);
    //*iterations = opt.iterations;
    for (i = 0; i < opt.num_threads; i++) {
        threads[i].args = malloc(sizeof(struct args));

        threads[i].args -> thread_num = i;
        threads[i].args -> array = arr;
        threads[i].args -> delay = opt.delay;
        threads[i].args -> iter = iter;
        threads[i].args->mutex = mutex;

        if (thrd_create(&threads[i].id, tipo, threads[i].args) != 0) {
            printf("Could not create thread #%d", i);
            exit(1);
        }
    }
    return threads;
}

void init_mutex(struct array *array, int num_reps) {
    int i;

    array->size = num_reps;
    array->mutex = malloc(array->size * sizeof(mtx_t));

    for(i = 0; i < array->size; i++) {
        array->arr[i] = 0;
        mtx_init(&array->mutex[i],0);
    }
}

void print_array(struct array arr) {
    int total = 0;

    for(int i = 0; i < arr.size; i++) {
        total += arr.arr[i];
        printf("%d ", arr.arr[i]);
    }

    printf("\nTotal: %d\n", total);
}

void wait(struct options opt, struct array *arr, struct thread_info *threads, struct iter *iter) {
    
    for (int i = 0; i < opt.num_threads; i++)
        thrd_join(threads[i].id, NULL);

    //Solo hace falta liberar la memoria del thread 0 porque comparten la posicion de memoria
    //de esos campos en todos los threads.

    mtx_destroy(threads[0].args->mutex);
    free(threads[0].args->mutex);

    for (int i = 0; i < opt.num_threads; i++)
        free(threads[i].args);

    free(threads);
    
    /*
        for(int j = 0; j < opt.size; j++) {
        mtx_destroy(&arr->mutex[j]);
    }
    */

}

int main (int argc, char **argv)
{
    struct options       opt;
    struct array         arr;
    struct thread_info *thrs;
    struct thread_info *thrs2;
    struct iter         iter1;
    struct iter         iter2;

    srand(time(NULL));

    // Default values for the options
    opt.num_threads  = 5;
    opt.size         = 10;
    opt.iterations   = 100;
    opt.delay        = 1000;

    read_options(argc, argv, &opt);

    iter1.iterador = opt.iterations;
    iter2.iterador = opt.iterations;

    mtx_init(&iter1.mutex,0);
    mtx_init(&iter2.mutex,0);

    arr.size = opt.size;
    arr.arr  = malloc(arr.size * sizeof(int));

    memset(arr.arr, 0, arr.size * sizeof(int));

    init_mutex(&arr, opt.size);
    thrs = startThreads(opt, &arr,increment2, &iter1); //creamos y ejecutamos los threads
    thrs2 = startThreads(opt,&arr,swap2, &iter2);      //creamos y ejecutamos los threads

    wait(opt, &arr, thrs, &iter1);               //finished threads and free
    wait(opt, &arr, thrs2, &iter2);             //finished threads and free
    
    print_array(arr);

    free(arr.mutex);
    free(arr.arr);
    
    return 0;
}