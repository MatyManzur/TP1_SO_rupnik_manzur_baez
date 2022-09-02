#define _BSD_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define SLAVE_COUNT 10

int main(int argc, char* argv[])
{
    if(argc!=2)
    {
        fprintf(stderr, "Cantidad de argumentos incorrecta! Debe ser uno.\n");
        exit(1);
    }

    int slavepids[SLAVE_COUNT] = {0};

    //Por cada slave
    for(int i=0 ; i<SLAVE_COUNT ; i++)
    {
        //crear dos pipes acá antes de separarse
        slavepids[i] = fork();
        if(slavepids[i] == -1)
        {
            //error con el fork
        }
        if(slavepids[i] == 0)
        {
            //estamos en el slave, setear los fd del pipe
            //si hacemos exec no hace falta salir del for porque ripea este codigo
        }
        //seguimos en el master, setear los fd del pipe
    }

    //argv[1] -> directorio

    //hacer lo mismo que con el tree (esperamos a que lo corrijan? lo corregiran?)
    //pero en vez de printearlo se lo pasamos al slave que esté desocupado -> usar select() para ver eso


    return 0;
}