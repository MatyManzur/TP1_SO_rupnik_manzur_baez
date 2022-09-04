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
        fprintf(stderr, "Error in argument count. Must be one.\n");
        exit(1);
    }

    int slavepids[SLAVE_COUNT] = {0};
    int pipefds[SLAVE_COUNT*2][2]; //por cada slave tenemos un pipe de ida (2*i) y un pipe de vuelta (2*i+1),
                                    // cada uno tiene dos file descriptors [0] (salida del pipe) y [1] (entrada del pipe)

    //Por cada slave
    for(int i=0 ; i<SLAVE_COUNT ; i++)
    {
        //Creamos dos pipes antes de hacer el fork
        if(pipe(pipefds[2*i])!=0 || pipe(pipefds[2*i+1])!=0)
        {
            perror("Error in creation of pipes");
            exit(1);
        }
        slavepids[i] = fork();
        if(slavepids[i] == -1)
        {
            perror("Forks in md5");
            exit(1);
        }
        if(slavepids[i] == 0)
        {
            //estamos en el slave, setear los fd del pipe

            dup2(pipefds[2*i][0], STDIN_FILENO); //cambiamos el stdin por la salida del pipe de ida
            dup2(pipefds[2*i+1][1], STDOUT_FILENO); //cambiamos el stdout por la entrada del pipe de vuelta

            //de este "i" tenemos que cerrar los 4 fds, y de los "i" anteriores tenemos que cerrar los dos que no cerro el master anteriores
            if(close(pipefds[2*i][0])!=0 || close(pipefds[2*i+1][1])!=0)
            {
                perror("Error in closure of pipes");
                exit(1);
            }
            for(int j=0; j<=i; j++)
            {
                if(close(pipefds[2*j][1])!=0 || close(pipefds[2*j+1][0])!=0)
                {
                    perror("Error in closure of pipes");
                    exit(1);
                }
            }
            execl("./md5Slave","./md5Slave",NULL);
            perror("Error in executing Slave");
            exit(1);
            //si hacemos exec no hace falta salir del for porque ripea este codigo, si dio error el exec, hacemos return
            //hacer chequeo si hacemos exec de que no siga el codigo, y si sigio tirar error
        }
        //seguimos en el master, setear los fd del pipe
        if(close(pipefds[2*i][0])!=0 || close(pipefds[2*i+1][1])!=0)
        {
            perror("Error in closure of pipes");
            exit(1);
        }
    }

    //argv[1] -> directorio
    //creamos los sets que select va a usar para monitorear
    int writefds[SLAVE_COUNT]={0};
    int readfds[SLAVE_COUNT]={0};

    for (int k = 0; k<SLAVE_COUNT ; ++k)//Seteamos los fds con los pipes correspondientes
    {
        writefds[k]=pipefds[2*k][1];
        readfds[k]=pipefds[2*k+1][0];
    }
    //hacer lo mismo que con el tree (esperamos a que lo corrijan? lo corregiran?)
    //pero en vez de printearlo se lo pasamos al slave que esté desocupado -> usar select() para ver eso


    return 0;
}
