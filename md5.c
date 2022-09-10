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

#define RESULT_FILE_NAME "Resultados.txt"

#define GOING_PIPE(slave) pipefds[2*(slave)]
#define RETURNING_PIPE(slave) pipefds[2*(slave)+1]

#define WRITING_END(pipe) (pipe)[1]
#define READING_END(pipe) (pipe)[0]

#define SHM_NAME "/oursharedmemory"
#define SHM_SIZE 4096
#define SEMAPHORE_NAME "/mysemaphore"
#define INITIAL_SEMAPHORE_VALUE 0

int initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[]);

int resetReadWriteFds(fd_set *readFds, fd_set *writeFds, FILE *readFiles[], FILE *writeFiles[], const int slaveReady[]);

void waitForSlaves(int slavepids[]);

void closePipes(FILE *wFiles[], FILE *rFiles[]);

FILE *createFile(char *name);

int writeInFile(FILE *file, char *string);

void closeFile(FILE *file);

void closeAllThings(FILE *wFiles[], FILE *rFiles[], FILE *output, ShmManagerADT shmManagerAdt, int slavepids[],
                    int semOpened);

int main(int argc, char *argv[])
{
    //Chequeamos que haya por lo menos un argumento
    if (argc < 2)
    {
        fprintf(stderr, "Error in argument count. Must be at least one.\n");
        exit(1);
    }

    //desactivamos el buffering de stdout para que se manden instantáneamente las cosas
    setvbuf(stdout, NULL, _IONBF, 0);

    int slavepids[SLAVE_COUNT] = {0};

    //pipefds tendrá los fileDescriptor de la entrada y salida de cada pipe a los slaves
    int pipefds[SLAVE_COUNT * 2][2];

    //wFiles y rFiles tendrán los FILE* de cada fileDescriptor utilizado por el master
    FILE *wFiles[SLAVE_COUNT];
    FILE *rFiles[SLAVE_COUNT];

    FILE *output;
    if ((output = createFile(RESULT_FILE_NAME)) == NULL)
    {
        exit(1);
    }

    //Esta función setea estas variables antes mencionadas
    if (initiatePipesAndSlaves(pipefds, slavepids, wFiles, rFiles) == -1)
    {
        closeAllThings(wFiles, rFiles, output, NULL, slavepids, 0);
        exit(1);
    }

    // inicializacion de shared memory y semáforos
    ShmManagerADT shmManagerAdt;
    if ((shmManagerAdt = newSharedMemoryManager(SHM_NAME, SHM_SIZE)) == NULL)
    {
        closeAllThings(wFiles, rFiles, output, NULL, slavepids, 0);
        exit(1);
    }

    if (createSharedMemory(shmManagerAdt) == -1)
    {
        closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 0);
        exit(1);
    }

    sem_t *semVistaReadyToRead = sem_open(SEMAPHORE_NAME, O_CREAT, O_CREAT | O_RDWR, INITIAL_SEMAPHORE_VALUE);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of the Semaphore");
        closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
        exit(1);
    }

    printf("%s\n%d\n%s\n", SHM_NAME, SHM_SIZE, SEMAPHORE_NAME);

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

    int messagesSentToShm = 0;

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
        //además resetReadFds() deja en los sets readFds y writeFds, los fd que queremos que select() se fije si estan disponibles

        //En teoría, los pipes de ida deberían estar vacíos si sabemos que están listos para escribir, por lo que select() no va a filtrar nada de este
        //set probablemente. Sin embargo, lo ponemos para que select() pueda continuar si todavía no hay nada para leer, pues hay cosas para escribir.

        //si ya no hay cosas para escribir, entonces sí debería quedarse esperando el select a recibir algo para leer,
        if (writtenFiles >= argc)
        {
            FD_ZERO(&writeFds); //por lo tanto vaciamos el writeFds set
        }

        //select esperará que haya por lo menos un pipe disponible para leer o escribir, y devolverá quienes son cuando los encuentre
        if (select(nfds, &readFds, &writeFds, NULL, NULL) == -1)
        {
            perror("Select Error");
            closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
            exit(1);
        }

        for (int i = 0; i < SLAVE_COUNT && (readSlave == -1 || writeSlave == -1); i++)
        {
            if (writeSlave < 0 && writtenFiles < argc && FD_ISSET(fileno(wFiles[i]), &writeFds))
            {
                writeSlave = i;
            }
            if (readSlave < 0 && FD_ISSET(fileno(rFiles[i]), &readFds))
            {
                readSlave = i;
            }
        }

        //si pasamos por todos y siguen valiendo -1 es porque no había ninguno disponible
        if (writeSlave >= 0)
        {
            //Obtenemos informacion del archivo a procesar
            struct stat statbuf;
            if (stat(argv[writtenFiles], &statbuf) == -1)
            {
                perror("Error in reading file");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            //si es un regular file, hacemos lo que tenemos que hacer
            if (S_ISREG(statbuf.st_mode))
            {
                size_t printValue = fwrite(argv[writtenFiles], 1, strlen(argv[writtenFiles]) + 1, wFiles[writeSlave]);
                if (printValue == 0)
                {
                    perror("Error in writing in pipe");
                    closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                    exit(1);
                }
                slaveReady[writeSlave] = 0; //como le mandamos algo para que trabaje, ahora está ocupado (=0)
            } else //si no es un reg file (directorio, etc.), md5sum no anda => lo salteamos
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
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            //Le agregamos quién fue, y lo escribimos en la shm y el archivo resultado
            if (snprintf(pid, PID_SIZE, "Slave PID:%d", slavepids[readSlave]) < 0)
            {
                perror("Error in creating slave pid string");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            strncat(s, pid, PID_SIZE);
            if (writeMessage(shmManagerAdt, s, ++readFiles >= argc) < 0)
            {
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            if (writeInFile(output, s) == -1)
            {
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            messagesSentToShm++;
            if (sem_post(semVistaReadyToRead) == -1)
            {
                perror("Error in posting to semaphore");
                closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
                exit(1);
            }
            slaveReady[readSlave] = 1; //como ya leímos lo que devolvió, ahora está libre (=1)
        }
    }

    //si no se mando ningun mensaje a la shm (por ej.: eran todos directorios), le avisamos al vista para que no se quede esperando
    if (messagesSentToShm == 0)
    {
        char *noFilesMsg = "No files were found!";
        if (writeMessage(shmManagerAdt, noFilesMsg, 1) < 0)
        {
            closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
            exit(1);
        }
        if (writeInFile(output, noFilesMsg) == -1)
        {
            closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
            exit(1);
        }
        if (sem_post(semVistaReadyToRead) == -1)
        {
            perror("Error in posting to semaphore");
            closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);
            exit(1);
        }
    }

    closeAllThings(wFiles, rFiles, output, shmManagerAdt, slavepids, 1);

    return 0;
}

void closeAllThings(FILE *wFiles[], FILE *rFiles[], FILE *output, ShmManagerADT shmManagerAdt, int slavepids[],
                    int semOpened)
{
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
        destroySharedMemory(shmManagerAdt);
        freeSharedMemoryManager(shmManagerAdt);
    }
    if (slavepids != NULL)
    {
        waitForSlaves(slavepids);
    }
    if (semOpened)
    {
        sem_unlink(SEMAPHORE_NAME);
    }
}

int initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[])
{
    //Por cada slave
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        //Creamos dos pipes antes de hacer el fork
        if (pipe(GOING_PIPE(i)) != 0 || pipe(RETURNING_PIPE(i)) != 0)
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
            //estamos en el slave

            dup2(READING_END(GOING_PIPE(i)), STDIN_FILENO); //cambiamos el stdin por la salida del pipe de ida
            dup2(WRITING_END(RETURNING_PIPE(i)), STDOUT_FILENO); //cambiamos el stdout por la entrada del pipe de vuelta

            //de este "i" tenemos que cerrar los 4 fds,
            if (close(READING_END(GOING_PIPE(i))) != 0 || close(WRITING_END(RETURNING_PIPE(i))) != 0)
            {
                perror("Error in closure of pipes");
                return -1;
            }
            //y de los "i" anteriores tenemos que cerrar los dos que no cerro el master anteriores
            for (int j = 0; j <= i; j++)
            {
                if (close(WRITING_END(GOING_PIPE(j))) != 0 || close(READING_END(RETURNING_PIPE(j))) != 0)
                {
                    perror("Error in closure of pipes");
                    return -1;
                }
            }
            execl("./md5Slave", "./md5Slave", NULL);
            perror("Error in executing Slave");
            return -1;
        }
        //seguimos en el master, cerramos los fd que no se usan
        if (close(READING_END(GOING_PIPE(i))) != 0 || close(WRITING_END(RETURNING_PIPE(i))) != 0)
        {
            perror("Error in closure of pipes");
            return -1;
        }
    }

    //guardamos los FILE* de cada fileDescriptor
    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        if ((wFiles[i] = fdopen(WRITING_END(GOING_PIPE(i)), "w")) == NULL)
        {
            perror("Error in creating Write Files");
            return -1;
        }
        if ((rFiles[i] = fdopen(READING_END(RETURNING_PIPE(i)), "r")) == NULL)
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