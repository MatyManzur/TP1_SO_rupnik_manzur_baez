#define _BSD_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/select.h>
#include <sys/time.h>

#define SLAVE_COUNT 10

int resetWriteReadFds(fd_set* writeFds,fd_set* readFds,int pipeFds[][2]);

int main(int argc, char* argv[])
{
    if(argc<2)
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
    fd_set * writeFds;
    FD_ZERO(writeFds);
    fd_set * readFds;
    FD_ZERO(readFds);
    fd_set * exceptFds; // No se usa pero para evitar errores al mandar un NULL
    FD_ZERO(exceptFds);


        // parte de shared memory
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_CREAT|O_RDWR, S_IRUSR|S_IWUSR))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if(ftruncate(fdsharedmem, SHM_SIZE)==-1) perror("Error trying to ftruncate");
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptowrite = (char*) addr_mapped;


    // parte de select
    int fileCounter = 1;
    // así sabemos cuándo termina
    while(fileCounter<argc)
    {
        
    }

    // esta es la parte de navegación del directorio

    /* Creo que no hace falta, directamente te pasan los archivos
    DIR * dirp = opendir(argv[1]);
    char path[64];
    path[0] = '\0';

    struct dirent * myDirent;
    struct stat sb;

    while(myDirent=readdir(dirp)!=NULL){
        if(!(strcmp(myDirent->d_name,"..")==0 || strcmp(myDirent->d_name,".")==0))
        {
            stat(myDirent->d_name,&sb);
        }
    }
    closedir(dirp);
    */

    //hacer lo mismo que con el tree (esperamos a que lo corrijan? lo corregiran?)
    //pero en vez de printearlo se lo pasamos al slave que esté desocupado -> usar select() para ver eso


    return 0;
}



//Setea los file descriptor writefds y readfds a los valores de los pipes, tambien calcula el nfds y lo devuelve
//Si los file descriptors estan en -1 entonces no los setea pues estan cerrados
int setWriteReadFds(fd_set* writeFds,fd_set* readFds,int pipeFds[][2])
{
    int nfds=0;
    for (int i = 0; i <SLAVE_COUNT; ++i)
    {
        if(pipeFds[2*i][1]!=-1)
        {
            FD_SET(pipeFds[2*i][1],writeFds);
            if(pipeFds[2*i][1]>=nfds) nfds=pipeFds[2*i][1];
        }
        if(pipeFds[2*i+1][0]!=-1)
        {
            FD_SET(pipeFds[2*i+1][0], readFds);
            if(pipeFds[2*i+1][0]>=nfds) nfds=pipeFds[2*i+1][0];
        }
    }
    return nfds;
}
