#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <openssl/evp.h>

#include "options.h"
#include "queue.h"
#include "threads.h"

#define MAX_PATH 1024
#define BLOCK_SIZE (10*1024*1024)
#define MAX_LINE_LENGTH (MAX_PATH * 2)

#define FALSE 0
#define TRUE 1

struct file_md5 {
    char *file;
    unsigned char *hash;
    unsigned int hash_size;
};

typedef struct {
	char* dir;      //directorio
	queue q;
}LectorSArgs;

typedef struct {
	queue   in_q;
	queue   out_q;
    mtx_t*  mtx_operativos;
    int*    n_operativos;
	int*    finLector;
	int*    n_Entries;
}opSArgs;

typedef struct {
    queue q;
	char* dir;
	char* file;
}EscrituraSArgs;

typedef EscrituraSArgs LeeCArgs; 

typedef struct {
    queue q;
} opCArgs;


void get_entries(char *dir, queue q);


void print_hash(struct file_md5 *md5) {
    for(int i = 0; i < md5->hash_size; i++) {
        printf("%02hhx", md5->hash[i]);
    }
}


void sum_file(struct file_md5 *md5) {
    EVP_MD_CTX *mdctx;
    int nbytes;
    FILE *fp;
    char *buf;

    if((fp = fopen(md5->file, "r")) == NULL) {
        printf("Could not open %s\n", md5->file);
        return;
    }

    buf = malloc(BLOCK_SIZE);
    const EVP_MD *md = EVP_get_digestbyname("md5");

    mdctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(mdctx, md, NULL);

    while((nbytes = fread(buf, 1, BLOCK_SIZE, fp)) >0)
        EVP_DigestUpdate(mdctx, buf, nbytes);

    md5->hash = malloc(EVP_MAX_MD_SIZE);
    EVP_DigestFinal_ex(mdctx, md5->hash, &md5->hash_size);

    EVP_MD_CTX_destroy(mdctx);
    free(buf);
    fclose(fp);
}


void recurse(char *entry, void *arg) {
    queue q = * (queue *) arg;
    struct stat st;

    stat(entry, &st);

    if(S_ISDIR(st.st_mode))
        get_entries(entry, q);
}


void add_files(char *entry, void *arg) {
    queue q = * (queue *) arg;
    struct stat st;

    stat(entry, &st);

    if(S_ISREG(st.st_mode)){
        q_insert(q, strdup(entry));
	}
}


void walk_dir(char *dir, void (*action)(char *entry, void *arg), void *arg) {
    DIR *d;
    struct dirent *ent;
    char full_path[MAX_PATH];

    if((d = opendir(dir)) == NULL) {
        printf("Could not open dir %s\n", dir);
        return;
    }

    while((ent = readdir(d)) != NULL) {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") ==0)
            continue;

        snprintf(full_path, MAX_PATH, "%s/%s", dir, ent->d_name);
        action(full_path, arg);
    }

    closedir(d);
}

//Funcion tipo hilo la cual lee las entradas del directorio para la funcion sum (-s)
int hiloLectorS(void* ptr){
	LectorSArgs* args = ptr;
	get_entries(args->dir, args->q);
    //cuando termina de insertar activamos el bit de desbloqueoRemove a true
	q_desbloqueo(args->q);  //no se sigue anhadiendo entradas a la cola
	return 0;
}

//Funcion tipo HILO que calcula el md5 de un fichero para la funcion sum (-s):
int obtainSum(void* ptr) {
	opSArgs* args = ptr;
	char *ent;
	struct file_md5 *md5;
    while((ent = q_remove(args->in_q)) != NULL) {
        md5 = malloc(sizeof(struct file_md5));

        md5->file = ent;
        sum_file(md5);

        q_insert(args->out_q, md5);
    }

	mtx_lock(args->mtx_operativos);

    //predecrementa el valor de operativos, si se cunple:
    //quiere decir que era el ultimo hilo de este tipo:
	if((--*args->n_operativos) == 0) {
        q_desbloqueo(args->out_q);
    }
         
	mtx_unlock(args->mtx_operativos);
	
	return 0;
}

//Funcion tipo HILO q escribe los hash md5 en un fichero para la funcion sum (-s):
int hacerEscritura(void* ptr) {
    EscrituraSArgs* args = ptr;
    struct file_md5 *md5;
    FILE *out;
    int dirname_len;
    
    if((out = fopen(args->file, "w")) == NULL) {
        printf("Could not open output file\n");
        exit(0);
    }

    dirname_len = strlen(args->dir) + 1; // length of dir + /

    while((md5 = q_remove(args->q)) != NULL) {
        fprintf(out, "%s: ", md5->file + dirname_len);

        for(int i = 0; i < md5->hash_size; i++)
            fprintf(out, "%02hhx", md5->hash[i]);
        fprintf(out, "\n");

        free(md5->file);
        free(md5->hash);
        free(md5);
    }

    fclose(out);
	
	return 0;
}

void get_entries(char *dir, queue q) {
    walk_dir(dir, add_files, &q);
    walk_dir(dir, recurse, &q);
}


void sum(struct options opt) {
    queue in_q, out_q;
    //char *ent;
    //FILE *out;
    //struct file_md5 *md5;
    //int dirname_len;
    thrd_t idL;
    LectorSArgs leeArgs;
    opSArgs opArgs;
    EscrituraSArgs escribeArgs;

    int ops_operativas;
    mtx_t mtx_ops_operativas;

    in_q  = q_create(opt.queue_size); 
    out_q = q_create(opt.queue_size);

    //Lectura:
    leeArgs.q = in_q;
	leeArgs.dir = opt.dir;
	//get_entries(opt.dir, in_q); convertimos get_entries en un hilo
	thrd_create(&idL,hiloLectorS,&leeArgs);
    
    //Calculo hash:
    thrd_t* idOps = malloc(opt.num_threads * sizeof(thrd_t));
    ops_operativas = opt.num_threads;
	mtx_init(&mtx_ops_operativas, mtx_plain);
    
	opArgs.out_q = out_q;
	opArgs.in_q = in_q;
	opArgs.n_operativos = &ops_operativas;
	opArgs.mtx_operativos = &mtx_ops_operativas;

	for(int i = 0; i < opt.num_threads; i++){
		thrd_create(&idOps[i],obtainSum,&opArgs);
	}

    //Escritura:
    thrd_t idE;
    escribeArgs.q = out_q;
    escribeArgs.dir = opt.dir;
    escribeArgs.file = opt.file;
    thrd_create(&idE, hacerEscritura, &escribeArgs);

    //finalizamos lectura
    thrd_join(idL, NULL);
    //finalizamos las operaciones
    for(int i = 0; i < opt.num_threads; i++) {
        thrd_join(idOps[i], NULL);
    }

    //finalizamos escritura:
    thrd_join(idE, NULL);

    //destroys:
    q_destroy(in_q);
    q_destroy(out_q);
    mtx_destroy(&mtx_ops_operativas);

}

void read_hash_file(char *file, char *dir, queue q) {
    FILE *fp;
    char line[MAX_LINE_LENGTH];
    char *file_name, *hash;
    int hash_len;

    if((fp = fopen(file, "r")) == NULL) {
        printf("Could not open %s : %s\n", file, strerror(errno));
        exit(0);
    }

    while(fgets(line, MAX_LINE_LENGTH, fp) != NULL) {
        char *field_break;
        struct file_md5 *md5 = malloc(sizeof(struct file_md5));

        if((field_break = strstr(line, ": ")) == NULL) {
            printf("Malformed md5 file\n");
            exit(0);
        }
        *field_break = '\0';

        file_name = line;
        hash      = field_break + 2;
        hash_len  = strlen(hash);

        md5->file      = malloc(strlen(file_name) + strlen(dir) + 2);
        sprintf(md5->file, "%s/%s", dir, file_name);
        md5->hash      = malloc(hash_len / 2);
        md5->hash_size = hash_len / 2;


        for(int i = 0; i < hash_len; i+=2)
            sscanf(hash + i, "%02hhx", &md5->hash[i / 2]);

        q_insert(q, md5);
    }

    fclose(fp);
}

//funcion que le el fichero que contiene los hashes en la op check (-c):
int hiloLectorC(void* ptr) {
    LeeCArgs* args = ptr;
    read_hash_file(args->file, args->dir, args->q);
    q_desbloqueo(args->q); //no se inserta mas
    return 0;
}

//funcion que obtiene los hash de la op. check (-c):
int obtainCheck(void* ptr) {
    opCArgs* args = ptr;
    struct file_md5 *md5_in, md5_file;
    while((md5_in = q_remove(args->q)) != NULL) {
        md5_file.file = md5_in->file;
        sum_file(&md5_file);

        if(memcmp(md5_file.hash, md5_in->hash, md5_file.hash_size)!=0) {
            printf("File %s doesn't match.\nFound:    ", md5_file.file);
            print_hash(&md5_file);
            printf("\nExpected: ");
            print_hash(md5_in);
            printf("\n");
        }

        free(md5_file.hash);

        free(md5_in->file);
        free(md5_in->hash);
        free(md5_in);
    }
	return 0;
}

void check(struct options opt) {
    queue in_q;
    //struct file_md5 *md5_in, md5_file;
    thrd_t idL;
    LeeCArgs leeArgs;
    opCArgs escribeArgs;
    in_q  = q_create(opt.queue_size);

    //read_hash_file(opt.file, opt.dir, in_q);
    //Faltaba hacer el desbloqueo de la cola:
    //q_desbloqueo(in_q);


    //Lectura:
    leeArgs.q = in_q;
    leeArgs.file = opt.file;
    leeArgs.dir = opt.dir;

    thrd_create(&idL, hiloLectorC, &leeArgs);

    //comenzamos obtainCheck:
    escribeArgs.q = in_q;
    thrd_t* idsOp = malloc(opt.num_threads * sizeof(thrd_t));

    for(int i = 0; i < opt.num_threads; i++) {
        thrd_create(&idsOp[i], obtainCheck, &escribeArgs);
    }

    /*
        while((md5_in = q_remove(in_q))) {
        md5_file.file = md5_in->file;

        sum_file(&md5_file);

        if(memcmp(md5_file.hash, md5_in->hash, md5_file.hash_size)!=0) {
            printf("File %s doesn't match.\nFound:    ", md5_file.file);
            print_hash(&md5_file);
            printf("\nExpected: ");
            print_hash(md5_in);
            printf("\n");
        }

        free(md5_file.hash);

        free(md5_in->file);
        free(md5_in->hash);
        free(md5_in);
    }
    */

   //finalizamos lector:
    thrd_join(idL, NULL);

    //finalizamos op:
    for(int i = 0; i < opt.num_threads; i++) {
        thrd_join(idsOp[i], NULL);
    }

    q_destroy(in_q);
}

int main(int argc, char *argv[]) {

    struct options opt;

    opt.num_threads = 5;
    opt.queue_size  = 1000;
    opt.check       = true;
    opt.file        = NULL;
    opt.dir         = NULL;

    read_options (argc, argv, &opt);

    if(opt.check)
        check(opt);
    else
        sum(opt);

}

