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
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#define SHM_NAME "/oursharedmemory"
#define SHM_SIZE 1024
#define SLAVE_COUNT 10
#define SEMAPHORE_NAME "/mysemaphore"
#define INITIAL_SEMAPHORE_VALUE 0

void initiatePipesAndSlaves(int pipefds[][2],int slavepids[]);
int resetWriteReadFds(fd_set* writeFds,fd_set* readFds,FILE* writeFiles[],FILE* readFiles[]);
void waitForSlaves(int slavepids[]);
int lenstrcpy(char dest[], char source[]);

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

    initiatePipesAndSlaves(pipefds,slavepids);

    FILE * wFiles[SLAVE_COUNT];
    FILE * rFiles[SLAVE_COUNT];

    for (int i = 0; i <SLAVE_COUNT; i++)
    {
        if( ( wFiles[i]=fdopen(pipefds[2*i][1],"w") ) == NULL)
        {
            perror("Error in creating Write Files");
            exit(1);
        }
        if( ( rFiles[i]=fdopen(pipefds[2*i+1][0],"r") ) == NULL)
        {
            perror("Error in creating Read Files");
            exit(1);
        }
        setvbuf(wFiles[i], NULL, _IONBF, 0); //Desactivamos el buffering del Pipe para que se envie de forma continua
    }
    
    // inicializacion de shared memory y semáforos
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_CREAT|O_RDWR, S_IRUSR|S_IWUSR))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if(ftruncate(fdsharedmem, SHM_SIZE)==-1) perror("Error trying to ftruncate");
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptowrite = (char*) addr_mapped;
    
    sem_t *semVistaReadyToRead = sem_open(SEMAPHORE_NAME,O_CREAT,O_CREAT|O_RDWR,INITIAL_SEMAPHORE_VALUE);
    if(semVistaReadyToRead==SEM_FAILED){
        perror("Error with Vista's opening of the Semaphore");
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
    int writtenFiles = 1;
    int readFiles = 1; //iteramos por los archivos recibidos por argumento
    while(readFiles < argc)
    {
        readSlave = -1; //inicialmente, no encontramos ningun slave para escribir ni para leer (=-1)
        writeSlave = -1;
        int nfds = resetWriteReadFds(&writeFds,&readFds,wFiles,rFiles);
        if(select(nfds,&readFds,&writeFds,&exceptFds,NULL)==-1)
        {
            perror("Select Error");
            exit(1);
        }
        for (int i = 0; i <SLAVE_COUNT && (readSlave==-1 || writeSlave==-1); i++)
        {
            if(slaveReady[i] && FD_ISSET(fileno(wFiles[i]),&writeFds)&& writtenFiles<argc)
            {
                writeSlave=i;
            }
            else
            {
                if(FD_ISSET(fileno(rFiles[i]),&readFds)!=0)
                {
                    readSlave=i;
                }
            }
        }
        //si pasamos por todos y siguen valiendo -1 es porque no había ninguno disponible
        if(writeSlave>=0)
        {
            //Obtenemos informacion del proximo archivo a procesar
            struct stat statbuf;
            stat(argv[writtenFiles], &statbuf);
            //si no es un directorio, hacemos lo que tenemos que hacer
            if(!S_ISDIR(statbuf.st_mode))
            {
                //DEBUG
                fprintf(stderr,"DEBUG: Sending to slave %d : %s\n", writeSlave, argv[writtenFiles]);
                //-----
                size_t printValue = fwrite(argv[writtenFiles],1,strlen(argv[writtenFiles])+1,wFiles[writeSlave]);
                if(printValue<=0)
                {
                    perror("Error in writing in pipe");
                    exit(1);
                }
                slaveReady[writeSlave] = 0; //como le mandamos algo para que trabaje, ahora está ocupado (=0)
                //fclose(writePipeFile);
            }
            else //si es un directorio, md5sum no anda => lo salteamos
            {
                readFiles++; //incrementamos readFiles porque sino nos va a faltar uno en la cuenta
            }
            writtenFiles++; //sea directorio o no, aumentamos el writtenFiles (ya sea porque lo enviamos a un slave, o porque lo salteamos)
        }

        if(readSlave>=0)
        {
            //Leemos del pipe de uno de los slaves que haya terminado

            char s[128];
            fgets(s,128,rFiles[readSlave]);
            //DEBUG
            fprintf(stderr,"DEBUG: Read from slave %d : %s\n", readSlave, s);
            //-----
            ptowrite+=(lenstrcpy(ptowrite,s)+1);
            sem_post(semVistaReadyToRead); 
            slaveReady[readSlave] = 1; //como ya leímos lo que devolvió, ahora está libre (=1)
            readFiles++;
        }
    }
    *ptowrite ='\n'; // Terminamos la linea que vamos a escribir y vista va saber cuando parar

    for(int i=0;i<SLAVE_COUNT;i++)
    {
        if( fclose(wFiles[i])!=0 || fclose(rFiles[i])!=0 )
        {
            perror("Error in closing pipes");
            exit(1);
        }
    }

    waitForSlaves(slavepids);
    
    //hacer lo mismo que con el tree (esperamos a que lo corrijan? lo corregiran?)
    //pero en vez de printearlo se lo pasamos al slave que esté desocupado -> usar select() para ver eso

    //  if(sem_unlink(SEMAPHORE_NAME)){ // al parecer esto es suficiente, quizás haya que hacer sem_close también
    //     perror("Error destroying semaphore(s)");
    //     exit(1);
    // }
    // if(munmap(addr_mapped,SHM_SIZE)==-1){
    //     perror("Error in destroying shared memory");
    //     exit(1);
    // }
    return close(fdsharedmem); // quizás haya que usar munmap y/o shm_unlink
}

void initiatePipesAndSlaves(int pipefds[][2],int slavepids[]){
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
}


//Setea los file descriptor writefds y readfds a los valores de los pipes, tambien calcula el nfds y lo devuelve
//Si los file descriptors estan en -1 entonces no los setea pues estan cerrados
int resetWriteReadFds(fd_set* writeFds,fd_set* readFds,FILE* writeFiles[],FILE* readFiles[])
{
    int nfds=0;
    FD_ZERO(writeFds);
    FD_ZERO(readFds);
    for (int i = 0; i <SLAVE_COUNT; ++i)
    {
        FD_SET(fileno(writeFiles[i]),writeFds);

        if(fileno(writeFiles[i])>=nfds) nfds=fileno(writeFiles[i]);
        
       
        FD_SET(fileno(readFiles[i]), readFds);

        if(fileno(readFiles[i])>=nfds) nfds=fileno(readFiles[i]);
    
    }
    return nfds + 1;
}

void waitForSlaves(int slavepids[]){
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
}

int lenstrcpy(char dest[], char source[]){
    int i=0;
    while(source[i]!='\0'){
        dest[i] = source[i];
        i++;
    }
    dest[i]='\0';
    return i;
}
