#define _BSD_SOURCE
#define _POSIX_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <semaphore.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#define SHM_NAME "/oursharedmemory"
#define SHM_SIZE 1024
#define SLAVE_COUNT 10
#define INITIAL_SEM1_VALUE 1
#define INITIAL_SEM2_VALUE 0
#define SEM_BETWEEN_PROCESSES 1

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
        if (pipe(pipefds[2 * i]) != 0 || pipe(pipefds[2 * i + 1]) != 0)
        {
            perror("Error in creation of pipes");
            exit(1);
        }
        slavepids[i] = fork();
        if (slavepids[i] == -1)
        {
            perror("Error in forks in md5");
            exit(1);
        }
        if (slavepids[i] == 0)
        {
            //estamos en el slave, setear los fd del pipe

            dup2(pipefds[2 * i][0], STDIN_FILENO); //cambiamos el stdin por la salida del pipe de ida
            dup2(pipefds[2 * i + 1][1], STDOUT_FILENO); //cambiamos el stdout por la entrada del pipe de vuelta

            //de este "i" tenemos que cerrar los 4 fds, y de los "i" anteriores tenemos que cerrar los dos que no cerro el master anteriores
            if (close(pipefds[2 * i][0]) != 0 || close(pipefds[2 * i + 1][1]) != 0)
            {
                perror("Error in closure of pipes");
                exit(1);
            }
            for (int j = 0; j <= i; j++)
            {
                if (close(pipefds[2 * j][1]) != 0 || close(pipefds[2 * j + 1][0]) != 0)
                {
                    perror("Error in closure of pipes");
                    exit(1);
                }
            }
            execl("./md5Slave", "./md5Slave", NULL);
            perror("Error in executing Slave");
            exit(1);
            //si hacemos exec no hace falta salir del for porque ripea este codigo, si dio error el exec, hacemos return

        }
        //seguimos en el master, cerramos los fd que no se usan
        if (close(pipefds[2 * i][0]) != 0 || close(pipefds[2 * i + 1][1]) != 0)
        {
            perror("Error in closure of pipes");
            exit(1);
        }
        //DEBUG
        fprintf(stderr,"DEBUG: Pipes for Slave %d created\n", i);
    }


    // inicializacion de shared memory y semáforos
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_CREAT|O_RDWR, S_IRUSR|S_IWUSR))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if(ftruncate(fdsharedmem, SHM_SIZE)==-1) perror("Error trying to ftruncate");
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptowrite = (char*) addr_mapped;
    ptowrite+=sizeof(sem_t)*2; //Hay que chequear esto, pero en teoría el sem_t ocupa 16 bytes

    sem_t *sem1 = (sem_t *) addr_mapped;
    sem_t *sem2 = sem1+1;
    if(sem_init(sem1,SEM_BETWEEN_PROCESSES,INITIAL_SEM1_VALUE)==-1 || sem_init(sem2,SEM_BETWEEN_PROCESSES,INITIAL_SEM2_VALUE)==-1)
    {
        perror("Error initiating a semaphore");
        exit(1);
    }


    // SELECT

    //argv[1,2,...] -> archivos
    //creamos los sets que select va a usar para monitorear cuales ya están disponibles
    fd_set writeFds;
    FD_ZERO(&writeFds);
    fd_set readFds;
    FD_ZERO(&readFds);
    fd_set exceptFds; // No se usa pero para evitar errores al mandar un NULL
    FD_ZERO(&exceptFds);

    int slaveReady[SLAVE_COUNT]; //Aca guardamos cuáles slaves están disponibles para escribir
    for (int i = 0; i <SLAVE_COUNT; i++)
    {
        slaveReady[i]=1;    // Inicialmente, todos los slaves pueden recibir archivos, estan disponibles (=1)
    }
    
    // vamos a buscar un slave disponible para escribir, y uno que ya esté listo para leer el resultado
    int readSlave;
    int writeSlave;
    int writtenFiles = 1; //iteramos por los archivos recibidos por argumento
    int readFiles = 1;
    while(writtenFiles<argc && readFiles<argc)
    {
        readSlave = -1; //inicialmente, no encontramos ningun slave para escribir ni para leer (=-1)
        writeSlave = -1;
        int nfds = resetWriteReadFds(&writeFds,&readFds,pipefds);
        if(select(nfds,&readFds,&writeFds,&exceptFds,NULL)==-1)
        {
            perror("Select Error");
            exit(1);
        }
        for (int i = 0; i <SLAVE_COUNT && (readSlave==-1 || writeSlave==-1); i++)
        {
            if(slaveReady[i] && FD_ISSET(pipefds[2*i][1],&writeFds))
            {
                writeSlave=i;
            }
            else
            {
                if(FD_ISSET(pipefds[2*i+1][0],&readFds)!=0)
                {
                    readSlave=i;
                }
            }
        }
        //si pasamos por todos y siguen valiendo -1 es porque no había ninguno disponible
        if(writeSlave>=0)
        {
            //Escribimos en el pipe de uno de los slaves que está listo
            FILE * writePipeFile = fdopen(pipefds[2*writeSlave][1],"w");
            if(writePipeFile==NULL)
            {
                perror("Error in writing on pipe");
                exit(1);
            }
            //DEBUG
            fprintf(stderr,"DEBUG: Sending to slave %d : %s\n", writeSlave, argv[fileCounter]);
            //-----
            fprintf(writePipeFile,"%s",argv[fileCounter]);
            slaveReady[writeSlave] = 0; //como le mandamos algo para que trabaje, ahora está ocupado (=0)
            fileCounter++;
        }

        if(readSlave>=0)
        {
            //Leemos del pipe de uno de los slaves que haya terminado
            FILE * readPipeFile = fdopen(pipefds[2*readSlave+1][0],"r");
            if(readPipeFile==NULL)
            {
                perror("Error in reading from pipe");
                exit(1);
            }
            char s[128];
            fgets(s,128,readPipeFile);
            //DEBUG
            fprintf(stderr,"DEBUG: Read from slave %d : %s\n", readSlave, s);
            //-----
            slaveReady[readSlave] = 1; //como ya leímos lo que devolvió, ahora está libre (=1)
        }
    }

    for(int i=0;i<SLAVE_COUNT;i++)
    {
        if(close(pipefds[2*i][1])!=0)
        {
            perror("Error in closing pipes");
            exit(1);
        }
        if(close(pipefds[2*i+1][0])!=0)
        {
            perror("Error in closing pipes");
            exit(1);
        }
    }

    for(int i=0;i<SLAVE_COUNT;i++)
    {
        int status = 0;
        if(waitpid(slavepids[i], &status, WUNTRACED | WCONTINUED)==-1)
        {
            perror("Error in waiting for slaves");
            exit(1);
        }
        if(status!=0)
            fprintf(stderr,"Error in slave %d : Exit with code %d\n", i, status);
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

    if(sem_destroy(sem1) || sem_destroy(sem2)){
        perror("Error destroying semaphore(s)");
        exit(1);
    }
    return close(fdsharedmem); // quizás haya que usar munmap y/o shm_unlink
}



//Setea los file descriptor writefds y readfds a los valores de los pipes, tambien calcula el nfds y lo devuelve
//Si los file descriptors estan en -1 entonces no los setea pues estan cerrados
int resetWriteReadFds(fd_set* writeFds,fd_set* readFds,int pipeFds[][2])
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
    return nfds + 1;
}
