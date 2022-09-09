// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#define _BSD_SOURCE
#define _POSIX_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <dirent.h>
#include "shm_manager.h"

#define SLAVE_COUNT 10
#define MD5_SIZE 32
#define PID_SIZE 32
#define BUFFER_SIZE (NAME_MAX+MD5_SIZE+PID_SIZE)


#define SHM_NAME "/oursharedmemory"
#define SHM_SIZE 1024
#define SEMAPHORE_NAME "/mysemaphore"
#define INITIAL_SEMAPHORE_VALUE 0

int initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[]);

int resetReadWriteFds(fd_set *readFds, fd_set *writeFds, FILE *readFiles[], FILE *writeFiles[], const int slaveReady[]);

void waitForSlaves(int slavepids[]);

void closePipes(FILE *wFiles[], FILE *rFiles[]);

FILE *createFile(char *name);

int writeInFile(FILE *file, char *string);

void closeFile(FILE *file);

void closeAllThings(FILE *wFiles[], FILE *rFiles[], FILE *output, ShmManagerADT shmManagerAdt, int slavepids[]);

int main(int argc, char *argv[])
{
    //Chequeamos que haya por lo menos un argumento
    if (argc < 2)
    {
        fprintf(stderr, "Error in argument count. Must be at least one.\n");
        exit(1);
    }

    //slavepids tendrá los pid de los slaves
    int slavepids[SLAVE_COUNT] = {0};

    //pipefds tendrá los fileDescriptor de la entrada y salida de cada pipe a los slaves
    //por cada slave tenemos un pipe de ida (2*i) y un pipe de vuelta (2*i+1),
    //cada uno tiene dos file descriptors [0] (salida del pipe) y [1] (entrada del pipe)
    int pipefds[SLAVE_COUNT * 2][2];

    //wFiles y rFiles tendrán los FILE* de cada fileDescriptor utilizado por el master
    //es decir, entradas de pipes de ida (wFiles) y salidas de pipes de vuelta (rFiles)
    //se guardan los FILE* además de los int fd's porque más adelante, funciones como fgets y fwrite
    //necesitan un FILE*. No hacemos un fdopen() en el momento a ser utilizado para obtener el FILE*
    //para poder hacer un fclose() al final, y no perder la referencia causando un memory leak
    FILE *wFiles[SLAVE_COUNT];
    FILE *rFiles[SLAVE_COUNT];

    FILE *output;
    if ((output = createFile("Resultados.txt")) == NULL)
    {
        exit(1);
    }

    //Esta función setea estas variables antes mencionadas
    if (initiatePipesAndSlaves(pipefds, slavepids, wFiles, rFiles) == -1)
    {
        closeAllThings(wFiles, rFiles, output, NULL, slavepids);
        exit(1);
    }

    // inicializacion de shared memory y semáforos
    ShmManagerADT shmManagerAdt;
    if ((shmManagerAdt = newSharedMemoryManager(SHM_NAME, SHM_SIZE)) == NULL)
    {
        closeAllThings(wFiles, rFiles, output, NULL, slavepids);
        exit(1);
    }

    if (createSharedMemory(shmManagerAdt) == -1)
    {
        closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
        exit(1);
    }

    sem_t *semVistaReadyToRead = sem_open(SEMAPHORE_NAME, O_CREAT, O_CREAT | O_RDWR, INITIAL_SEMAPHORE_VALUE);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of the Semaphore");
        closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
        exit(1);
    }

    printf("%s\n%d\n%s\n%d\n", SHM_NAME, SHM_SIZE, SEMAPHORE_NAME, INITIAL_SEMAPHORE_VALUE);
    sleep(2);

    // SELECT

    //en argv[1,2,...] tenemos los nombres de los archivos

    //slaveReady[i] == 1 si el slave i está listo para recibir otro archivo, o será == 0 si está ocupado
    int slaveReady[SLAVE_COUNT];
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        slaveReady[i] = 1;    // Inicialmente, todos los slaves pueden recibir archivos, estan disponibles (=1)
    }

    //contamos la cantidad de archivos pasados a los slaves (writtenFiles) o salteados por ser directorios
    //y la cantidad de md5 recibidos de los slaves (readFiles) o salteados por ser directorios
    int writtenFiles = 1, readFiles = 1;
    //(comienzan en 1 pues argc está contando un argumento de más con el nombre del programa)

    //iteramos por los archivos recibidos por argumento
    while (readFiles < argc) // no vamos a parar hasta haber recibido los md5 de todos los archivos no salteados
    {
        // vamos a buscar un slave disponible para escribir, y uno que ya esté listo para leer el resultado
        int readSlave = -1, writeSlave = -1;

        //creamos los sets que usará select() para monitorear los pipes que ya tienen contenido para leer, y los disponibles para escribir
        fd_set readFds;
        fd_set writeFds;

        //nfds --> fd más alto de los que estamos usando + 1 (pedido por select)
        int nfds = resetReadWriteFds(&readFds, &writeFds, rFiles, wFiles, slaveReady);
        //además resetReadFds() deja en los sets readFds y writeFds, los fd con que queremos que el select trabaje
        //a readFds solo le agregamos los fd de los procesos que sabemos que están ocupados, porque son los que esperamos que escriban en el pipe.
        //a writeFds solo le agregamos los fd de los procesos que sabemos que están listos para escribir.
        //En teoría, los pipes de ida deberían estar vacíos si sabemos que están listos para escribir, por lo que select() no va a filtrar nada de este
        //set probablemente. Sin embargo, lo ponemos para que select() pueda continuar si todavía no hay nada para leer, pues hay cosas para escribir.

        //si ya no hay cosas para escribir, entonces sí debería quedarse esperando el select a recibir algo para leer,
        //por lo tanto vaciamos el writeFds set
        if (writtenFiles >= argc)
        {
            FD_ZERO(&writeFds);
        }

        //select esperará que haya por lo menos un pipe disponible para leer o escribir, y devolverá quienes son cuando los encuentre
        if (select(nfds, &readFds, &writeFds, NULL, NULL) == -1)
        {
            perror("Select Error");
            closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
            exit(1);
        }

        for (int i = 0; i < SLAVE_COUNT && (readSlave == -1 || writeSlave == -1); i++)
        {
            //sabemos que el pipe de ida está vacío si es que está ready, pues ya devolvió el md5 anterior,
            //y si lo hizo es porque tuvo que leer del pipe de ida, por lo que éste debería estar vacío

            //entonces si todavía no encontramos uno disponible, y todavía quedan files por enviar y sabemos que ese slave i está disponible (lo pusimos en el writeFds)
            if (writeSlave < 0 && writtenFiles < argc && FD_ISSET(fileno(wFiles[i]), &writeFds))
            {
                writeSlave = i; //lo agarramos para escribir
            }
            //si el pipe ya tiene cosas para leer, es porque terminó
            if (readSlave < 0 && FD_ISSET(fileno(rFiles[i]), &readFds))
            {
                readSlave = i; //lo agarramos para leer
            }
        }
        //si pasamos por todos y siguen valiendo -1 es porque no había ninguno disponible
        if (writeSlave >= 0)
        {
            //Obtenemos informacion del archivo a procesar
            struct stat statbuf;
            if (stat(argv[writtenFiles], &statbuf) == -1)
            {
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            //si no es un directorio, hacemos lo que tenemos que hacer
            if (!S_ISDIR(statbuf.st_mode))
            {
                size_t printValue = fwrite(argv[writtenFiles], 1, strlen(argv[writtenFiles]) + 1, wFiles[writeSlave]);
                if (printValue == 0)
                {
                    perror("Error in writing in pipe");
                    closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                    exit(1);
                }
                slaveReady[writeSlave] = 0; //como le mandamos algo para que trabaje, ahora está ocupado (=0)
            } else //si es un directorio, md5sum no anda => lo salteamos
            {
                readFiles++; //incrementamos readFiles porque sino nos va a faltar uno en la cuenta
            }
            writtenFiles++; //sea directorio o no, aumentamos el writtenFiles (ya sea porque lo enviamos a un slave, o porque lo salteamos)
        }

        if (readSlave >= 0)
        {
            //Leemos del pipe de uno de los slaves que haya terminado
            char s[BUFFER_SIZE];
            char pid[PID_SIZE];
            if (fgets(s, NAME_MAX + MD5_SIZE, rFiles[readSlave]) == NULL)
            {
                perror("Error in reading from pipe");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            if (snprintf(pid, PID_SIZE, "Slave PID:%d", slavepids[readSlave]) < 0)
            {
                perror("Error in creating slave pid string");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            strncat(s, pid, PID_SIZE);
            if (writeMessage(shmManagerAdt, s, ++readFiles >= argc) < 0)
            {
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            if (writeInFile(output, s) == -1)
            {
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            if (sem_post(semVistaReadyToRead) == -1)
            {
                perror("Error in posting to semaphore");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);
                exit(1);
            }
            slaveReady[readSlave] = 1; //como ya leímos lo que devolvió, ahora está libre (=1)
        }
    }

    closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids);

    return 0;
}

void closeAllThings(FILE *wFiles[], FILE *rFiles[], FILE *output, ShmManagerADT shmManagerAdt, int slavepids[])
{
    //Cerramos los pipes
    if (wFiles != NULL && rFiles != NULL)
    {
        closePipes(wFiles, rFiles);
    }
    if (output != NULL)
    {
        closeFile(output);
    }
    if (shmManagerAdt != NULL)
    {
        disconnectFromSharedMemory(shmManagerAdt);
        freeSharedMemoryManager(shmManagerAdt);
    }
    if (slavepids != NULL)
    {
        waitForSlaves(slavepids);
    }
}

int initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[])
{
    //Por cada slave
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        //Creamos dos pipes antes de hacer el fork
        if (pipe(pipefds[2 * i]) != 0 || pipe(pipefds[2 * i + 1]) != 0)
        {
            perror("Error in creation of pipes");
            return -1;
        }
        slavepids[i] = fork();
        if (slavepids[i] == -1)
        {
            perror("Error in forks in md5");
            return -1;
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
                return -1;
            }
            for (int j = 0; j <= i; j++)
            {
                if (close(pipefds[2 * j][1]) != 0 || close(pipefds[2 * j + 1][0]) != 0)
                {
                    perror("Error in closure of pipes");
                    return -1;
                }
            }
            execl("./md5Slave", "./md5Slave", NULL);
            perror("Error in executing Slave");
            return -1;
            //si hacemos exec no hace falta salir del for porque ripea este codigo, si dio error el exec, hacemos return

        }
        //seguimos en el master, cerramos los fd que no se usan
        if (close(pipefds[2 * i][0]) != 0 || close(pipefds[2 * i + 1][1]) != 0)
        {
            perror("Error in closure of pipes");
            return -1;
        }
    }

    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        if ((wFiles[i] = fdopen(pipefds[2 * i][1], "w")) == NULL)
        {
            perror("Error in creating Write Files");
            return -1;
        }
        if ((rFiles[i] = fdopen(pipefds[2 * i + 1][0], "r")) == NULL)
        {
            perror("Error in creating Read Files");
            return -1;
        }
        setvbuf(wFiles[i], NULL, _IONBF, 0); //Desactivamos el buffering del Pipe para que se envie de forma continua
    }
    return 0;
}

//Agrega los fds de los pipes que correspondan en los readFds y writeFds sets, tambien calcula el nfds y lo devuelve
//Agregará los no ready a readFds, y los ready a writeFds
int resetReadWriteFds(fd_set *readFds, fd_set *writeFds, FILE *readFiles[], FILE *writeFiles[], const int slaveReady[])
{
    int nfds = 0;
    FD_ZERO(readFds);
    FD_ZERO(writeFds);
    for (int i = 0; i < SLAVE_COUNT; ++i)
    {
        int fd;
        fd_set *correspondingSet;
        if (slaveReady[i])
        {
            fd = fileno(writeFiles[i]);
            correspondingSet = writeFds;
        } else
        {
            fd = fileno(readFiles[i]);
            correspondingSet = readFds;
        }

        FD_SET(fd, correspondingSet);
        if (fd >= nfds)
            nfds = fd;
    }
    return nfds + 1;
}

void closePipes(FILE *wFiles[], FILE *rFiles[])
{
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        if (fclose(wFiles[i]) != 0 || fclose(rFiles[i]) != 0)
        {
            perror("Error in closing pipes");
            exit(1);
        }
    }
}

void waitForSlaves(int slavepids[])
{
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        int status = 0;
        if (waitpid(slavepids[i], &status, WUNTRACED | WCONTINUED) == -1)
        {
            perror("Error in waiting for slaves");
            exit(1);
        }
        if (status != 0)
            fprintf(stderr, "Error in slave %d : Exit with code %d\n", i, status);
    }
}


FILE *createFile(char *name)
{
    FILE *out = fopen(name, "w");
    if (out == NULL)
    {
        perror("Error in creating file");
        return NULL;
    }
    return out;
}

int writeInFile(FILE *file, char *string)
{
    int amount = fprintf(file, "%s\n", string);
    if (amount <= 0)
    {
        perror("Error in writing file");
        return -1;
    }
    return amount;
}

void closeFile(FILE *file)
{
    if (fclose(file) == EOF)
    {
        perror("Error in closing file");
        exit(1);
    }
}