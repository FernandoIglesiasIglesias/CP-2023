#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>
#include <math.h>

#define DEBUG 2

/* Translation of the DNA bases
   A -> 0
   C -> 1
   G -> 2
   T -> 3
   N -> 4*/

#define M  31 // Number of sequences
#define N  20  // Number of bases per sequence

unsigned int g_seed = 0;

int fast_rand(void) {
    g_seed = (214013*g_seed+2531011);
    return (g_seed>>16) % 5;
}

// The distance between two bases
int base_distance(int base1, int base2){

  if((base1 == 4) || (base2 == 4)){
    return 3;
  }

  if(base1 == base2) {
    return 0;
  }

  if((base1 == 0) && (base2 == 3)) {
    return 1;
  }

  if((base2 == 0) && (base1 == 3)) {
    return 1;
  }

  if((base1 == 1) && (base2 == 2)) {
    return 1;
  }

  if((base2 == 2) && (base1 == 1)) {
    return 1;
  }

  return 2;
}

int main(int argc, char *argv[] ) {

  int i, j, numprocs, rank, rows;

  //variables para calcular posteriormente
  //la media de los tiempos de cada proceso
  int sumcompu = 0, sumcomu = 0;

  int *data1, *data2;
  int *localData1, *localData2;
  unsigned int *result;
  struct timeval comu1, comu2, comu3, comu4, compu1, compu2;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs); //obtenemos numero de procesos
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); //obtenemos id del proceso

  //empezamos padding:
  rows = ceil(M/numprocs);

  result = (int *) malloc((rows*numprocs)*sizeof(int));

  /* Initialize Matrices */
  if(rank==0) {
    data1 = (int *) malloc(M*N*sizeof(int));
    data2 = (int *) malloc(M*N*sizeof(int));
    for(i=0;i<M;i++) {
      for(j=0;j<N;j++) {
        /* random with 20% gap proportion */
        data1[i*N+j] = fast_rand();
        data2[i*N+j] = fast_rand();
      }
    } 
  }

  localData1 = (int *) malloc((rows*N)*sizeof(int));
  localData2 = (int *) malloc((rows*N)*sizeof(int));

  //primer tiempo comunicacion:
  gettimeofday(&comu1, NULL);

  //MPI_Scatter(buff ,sendcnt ,sendtype, recvbuff , recvcnt , recvtype ,root ,comm)
  MPI_Scatter(data1, (N*rows), MPI_INT, localData1, (N*rows), MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Scatter(data2, (N*rows), MPI_INT, localData2, (N*rows), MPI_INT, 0, MPI_COMM_WORLD);
  
  //segundo tiempo de comunicacion:
  gettimeofday(&comu2, NULL);

  //primer tiempo de computacion:
  gettimeofday(&compu1, NULL);

  //MUY IMPORTANTE MIENTRAS i<rows
  for(i=0;i<rows;i++) {
    result[i]=0;
    for(j=0;j<N;j++) {
      result[i] += base_distance(localData1[i*N+j], localData2[i*N+j]);
    }
  }
  //segundo tiempo de computacion
  gettimeofday(&compu2, NULL);
    
  //calculo tiempo computacion:
  int microseconds = (compu2.tv_usec - compu1.tv_usec)+ 1000000 * (compu2.tv_sec - compu1.tv_sec);

  //tercer tiempo de comunicacion:
  gettimeofday(&comu3, NULL);

  //MPI_Gather(buff,sendcnt,sendtype,recvbuff,recvcnt,recvtype,root(0),comm) 
  MPI_Gather(result, rows, MPI_INT, result, rows, MPI_INT, 0, MPI_COMM_WORLD);

  //ultimo tiempo de comunicacion
  gettimeofday(&comu4, NULL);

  //calculo tiempo comunicacion:
  int comunicacion = (comu2.tv_usec - comu1.tv_usec + comu4.tv_usec - comu3.tv_usec)+ 1000000 * (comu2.tv_sec - comu1.tv_sec + comu4.tv_sec - comu3.tv_sec);

  //Reducimos los tiempos de computacion y comunicacion al proceso 0
  //y hacemos una media de los tiempos de cada proceso
  //MPI_Reduce(buf,         recvbuf, count,datatype, op,   root,  comm)
  MPI_Reduce(&microseconds, &sumcompu, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&comunicacion, &sumcomu, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD); 

  /* Display result */
  if(rank==0) {
    if (DEBUG == 1) {
      unsigned int checksum = 0;
      for(i=0;i<M;i++) {
        checksum += result[i];
      }
      printf("Checksum: %d\n ", checksum);
    } else if (DEBUG == 2) {
        for(i=0;i<M;i++) {
        printf(" %d \t ",result[i]);
      }
    } else {
          printf("Tiempo medio de computacion  de los %d procesos: %lf\n", numprocs, (double) (sumcompu/numprocs)/1E6);
          printf("Tiempo medio de comunicacion de los %d procesos: %lf\n", numprocs, (double) (sumcomu/numprocs)/1E6);
  }
  }
   

  //liberamos solo en el proceso 0 pq solo se reserva en ese proceso
  if(rank==0) {
    free(data1); free(data2);
  }
  
  free(localData1);
  free(localData2);
  free(result);

  MPI_Finalize();

  return 0;
}

