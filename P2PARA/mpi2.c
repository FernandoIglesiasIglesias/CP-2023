#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>

void inicializaCadena(char *cadena, int n){
  int i;
  for(i=0; i<n/2; i++){
    cadena[i] = 'A';
  }
  for(i=n/2; i<3*n/4; i++){
    cadena[i] = 'C';
  }
  for(i=3*n/4; i<9*n/10; i++){
    cadena[i] = 'G';
  }
  for(i=9*n/10; i<n; i++){
    cadena[i] = 'T';
  }
}

//SUPONEMOS QUE LA OPERACION ES SIEMPRE UNA SUMA
//RECOLECCION DE COUNT:
int MPI_FlattreeColectiva(void *buff, void *recvbuff, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
  int i, numprocs, rank;
  int final_count, count_aux;

  int checkerror;

  MPI_Status status;
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if(rank == root) { 

    final_count = *(int *) buff; //hacemos el cast del buffer para pasarlo a int

    for(i = 0; i < numprocs; i++) {
      //comprobamos que no estemos en el proceso root
      if(i != root) {
        //el root recibe los valores de los otros procesos:
        //MPI_Recv(buff, count, datatype, source(i), tag, comm, status)
        checkerror = MPI_Recv(&count_aux, count, datatype, i, 0, comm, &status);

        if(checkerror != MPI_SUCCESS) return checkerror;

        final_count += count_aux;
      }
    }

    *(int *) recvbuff = final_count; //hacemos el cast del recvbuffer

  } else { //si es un proceso esclavo mandamos los datos al root
    //MPI_Send(buff, count, datatype, procdestino, tag, comm)
    checkerror = MPI_Send(buff, 1, datatype, root, 0, comm);

    //control del error:
    if(checkerror != MPI_SUCCESS) return checkerror;
  }
  return MPI_SUCCESS;
}

//DISTRIBUCION DE N Y L:
int MPI_BinomialColectiva(void *buff, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {

    int i, numprocs, rank, checkerror, rankdestino, rankorigen;

    MPI_Status status;

    MPI_Comm_size(MPI_COMM_WORLD, &numprocs); //Obtenemos el número de procesos
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);   //Obtenemos el número de proceso

    //Las iteraciones que hace el bucle es 
    //el int mas alto del logaritmo del numero de procesos.
    //SE ASUME QUE EL ROOT ES 0
    for (i = 1; i <= ceil(log2(numprocs)); ++i) {

        //COMUNICADORES:
        if(rank < pow(2, i-1)) { 
            //procesos con rank <2^i-1 se comunican con:
            //Calculo del destino
            rankdestino = rank + pow(2, i-1);

            //Se comprueba que el proceso que va a recibir el mensaje esté dentro del rango total de procesos
            if(rankdestino < numprocs) {
                //MPI_Send(buff, count, datatype, procdestino(rankdestino), tag, comm)
                checkerror = MPI_Send(buff, count, datatype, rankdestino, 0, comm);
                
                //control del error:
                if(checkerror != MPI_SUCCESS) return checkerror;
            }

        } else { //RECEPTORES:

            //Se comprueba que el proceso que llega es un receptor correcto.
            if(rank < pow(2, i)){
                //Calculamos el proceso origen
                rankorigen = rank - pow(2, i-1);

                //MPI_Recv(buff, count, datatype, source(rankorigen), tag, comm, status)
                checkerror = MPI_Recv(buff, count, datatype, rankorigen, 0, comm, &status);

                //control del error:
                if(checkerror != MPI_SUCCESS) return checkerror;
            }
        }
    }
    return MPI_SUCCESS;
}


int main(int argc, char *argv[])
{
  int i, n, count=0, numprocs, rank;
  char *cadena;
  char L;
  MPI_Status status;

  //Inicializamos MPI
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if(rank == 0){ //proceso maestro (0)

    if(argc != 3){ //comprobacion paso de parametros, solo es necesario hacerlo con el proceso 0
      printf("Numero incorrecto de parametros\nLa sintaxis debe ser: program n L\n  program es el nombre del ejecutable\n  n es el tamaño de la cadena a generar\n  L es la letra de la que se quiere contar apariciones (A, C, G o T)\n");
      exit(1);
    }

    n = atoi(argv[1]);
    L = *argv[2];

    cadena = (char *) malloc(n*sizeof(char));
    inicializaCadena(cadena, n);

    //TRANSMISION de n y L:
    //MPI_Bcast(buf, count, datatype, root, comm)
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&L, 1, MPI_CHAR, 0, MPI_COMM_WORLD);
    //MPI_BinomialColectiva(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    //MPI_BinomialColectiva(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);

    //trabajo proceso 0
    for(i=0; i<n; i+=numprocs){ 
      if(cadena[i] == L){
        count++;
      }
    }

    int final_count = 0;

    //REDUCCION DE LOS RESULTADOS:
    //MPI_Reduce(buf, recvbuf, count, datatype, op, root, comm)
    MPI_Reduce(&count, &final_count, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    //MPI_FlattreeColectiva(&count, &final_count, 1, MPI_INT, 0, MPI_COMM_WORLD);

    printf("El numero de apariciones de la letra %c es %d\n", L, final_count);
    free(cadena);

  } else { //procesos esclavos (>0)

    //RECEPCION de los datos que nos mandó el proceso maestro:
    //MPI_Bcast(buf, count, datatype, root, comm)
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&L, 1, MPI_CHAR, 0, MPI_COMM_WORLD);
    //MPI_BinomialColectiva(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    //MPI_BinomialColectiva(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);


    cadena = (char *) malloc(n*sizeof(char));
    inicializaCadena(cadena, n);

    count = 0;

    //trabajo procesos esclavos
    for(i=rank; i<n; i+=numprocs){
      if(cadena[i] == L){
        count++; 
      }
    }

    //MPI_Reduce(buf, recvbuf, count, datatype, op, root, comm)
    MPI_Reduce(&count, &count, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    //MPI_FlattreeColectiva(&count, &count, 1, MPI_INT, 0, MPI_COMM_WORLD);

    free(cadena);

  }

  //Finalizamos MPI:
  MPI_Finalize();
  exit(0);
}
