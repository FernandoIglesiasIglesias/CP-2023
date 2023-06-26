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

    //Enviamos los datos a los procesos esclavos (rank>0)
    for(i=1; i<numprocs; i++){ 

      //MPI_Send(buff, count, datatype, procdestino(i), tag, comm) 
      MPI_Send(&n, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
      MPI_Send(&L, 1, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }

    for(i=0; i<n; i+=numprocs){ //trabajo que solo hace el proceso 0
      if(cadena[i] == L){
        count++;
      }
    }

    //El proceso 0 recibe los resultados de los procesos esclavos (rank>0)
    for(i=1; i<numprocs; i++){ 
      int count_i; //variable auxiliar

      //MPI_Recv(buff, count, datatype, source(i), tag, comm, status)
      MPI_Recv(&count_i, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status); 
      //i es el proceso que manda el resultado

      count += count_i; //guardamos el resultado final en count
    }

    printf("El numero de apariciones de la letra %c es %d\n", L, count);
    free(cadena);

  } else { //procesos esclavos (>0)

    //recibimos los datos que nos mando el proceso maestro
    //source es 0 siempre porque es el proceso que hace el send
    //MPI_Recv(buff, count, datatype, source(0), tag, comm, status)
    MPI_Recv(&n, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
    MPI_Recv(&L, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

    cadena = (char *) malloc(n*sizeof(char));
    inicializaCadena(cadena, n);

    count = 0;

    //trabajo procesos esclavos
    for(i=rank; i<n; i+=numprocs){
      if(cadena[i] == L){
        count++; 
      }
    }

    //enviamos el resultado del proceso esclavo(rank>0) al proceso maestro (rank=0)
    //MPI_Send(buff, count, datatype, procdestino(0), tag, comm) 
    MPI_Send(&count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD); 
    free(cadena);

  }

  //Finalizamos MPI:
  MPI_Finalize();
  exit(0);
}
