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
#include <sys/wait.h>

#define SHM_NAME "/oursharedmemory"
#define SHM_SIZE 1024
#define SLAVE_COUNT 10
#define SEMAPHORE_NAME "/mysemaphore"
#define INITIAL_SEMAPHORE_VALUE 0

void initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[]);

int resetReadFds(fd_set *readFds, FILE *readFiles[], const int slaveReady[]);

void waitForSlaves(int slavepids[]);

void closePipes(FILE *wFiles[], FILE *rFiles[]);

int lenstrcpy(char dest[], const char source[]);

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

    //Esta función setea estas variables antes mencionadas
    initiatePipesAndSlaves(pipefds, slavepids, wFiles, rFiles);

    // inicializacion de shared memory y semáforos
    int fdsharedmem;
    void *addr_mapped;
    if ((fdsharedmem = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1)
        perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if (ftruncate(fdsharedmem, SHM_SIZE) == -1) perror("Error trying to ftruncate");
    if ((addr_mapped = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fdsharedmem, 0)) == MAP_FAILED)
        perror("Problem mapping shared memory");
    char *ptowrite = (char *) addr_mapped;

    sem_t *semVistaReadyToRead = sem_open(SEMAPHORE_NAME, O_CREAT, O_CREAT | O_RDWR, INITIAL_SEMAPHORE_VALUE);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of the Semaphore");
        exit(1);
    }

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

        //creamos el set que usará select() para monitorear los pipes que ya tienen contenido para leer
        fd_set readFds;

        //nfds --> fd más alto de los que estamos usando + 1 (pedido por select)
        int nfds = resetReadFds(&readFds, rFiles, slaveReady);
        //además resetReadFds() deja en el set readFds, los fd con que queremos que el select trabaje
        //solo le agregamos los fd de los procesos que sabemos que están ocupados, porque son los que esperamos que escriban en el pipe

        //select esperará que haya por lo menos un pipe disponible para leer o escribir, y devolverá quienes son cuando los encuentre
        if (select(nfds, &readFds, NULL, NULL, NULL) == -1)
        {
            perror("Select Error");
            exit(1);
        }

        for (int i = 0; i < SLAVE_COUNT && (readSlave == -1 || writeSlave == -1); i++)
        {
            //sabemos que el pipe de ida está vacío si está ready, porque entonces ya devolvió el md5 anterior,
            //y si lo hizo es porque tuvo que leer del pipe de ida, por lo que éste debería estar vacío

            //entonces si todavía no encontramos uno disponible, y todavía quedan files por enviar y sabemos que ese slave i está disponible
            if (writeSlave < 0 && writtenFiles < argc && slaveReady[i])
            {
                writeSlave = i; //lo agarramos
            }
            //si todavía no encontramos uno disponible para escribir y el pipe ya tiene cosas para leer, es porque terminó
            if (readSlave < 0 && FD_ISSET(fileno(rFiles[i]), &readFds) != 0)
            {
                readSlave = i; //lo agarramos para leer
            }
        }
        //si pasamos por todos y siguen valiendo -1 es porque no había ninguno disponible
        if (writeSlave >= 0)
        {
            //Obtenemos informacion del archivo a procesar
            struct stat statbuf;
            stat(argv[writtenFiles], &statbuf);
            //si no es un directorio, hacemos lo que tenemos que hacer
            if (!S_ISDIR(statbuf.st_mode))
            {
                size_t printValue = fwrite(argv[writtenFiles], 1, strlen(argv[writtenFiles]) + 1, wFiles[writeSlave]);
                if (printValue <= 0)
                {
                    perror("Error in writing in pipe");
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

            char s[128];
            fgets(s, 128, rFiles[readSlave]);
            ptowrite += (lenstrcpy(ptowrite, s) + 1);
            sem_post(semVistaReadyToRead);
            slaveReady[readSlave] = 1; //como ya leímos lo que devolvió, ahora está libre (=1)
            readFiles++;
        }
    }
    *ptowrite = '\n'; // Terminamos la linea que vamos a escribir y vista va saber cuando parar

    //Cerramos los pipes
    closePipes(wFiles, rFiles);

    //Esperamos por los esclavos a que terminen
    waitForSlaves(slavepids);

    return close(fdsharedmem);
}

void initiatePipesAndSlaves(int pipefds[][2], int slavepids[], FILE *wFiles[], FILE *rFiles[])
{
    //Por cada slave
    for (int i = 0; i < SLAVE_COUNT; i++)
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
        fprintf(stderr, "DEBUG: Pipes for Slave %d created\n", i);
    }

    for (int i = 0; i < SLAVE_COUNT; i++)
    {
        if ((wFiles[i] = fdopen(pipefds[2 * i][1], "w")) == NULL)
        {
            perror("Error in creating Write Files");
            exit(1);
        }
        if ((rFiles[i] = fdopen(pipefds[2 * i + 1][0], "r")) == NULL)
        {
            perror("Error in creating Read Files");
            exit(1);
        }
        setvbuf(wFiles[i], NULL, _IONBF, 0); //Desactivamos el buffering del Pipe para que se envie de forma continua
    }
}


//Setea los file descriptor writefds y readfds a los valores de los pipes, tambien calcula el nfds y lo devuelve
//Si los file descriptors estan en -1 entonces no los setea pues estan cerrados
int resetReadFds(fd_set *readFds, FILE *readFiles[], const int slaveReady[])
{
    int nfds = 0;
    FD_ZERO(readFds);
    for (int i = 0; i < SLAVE_COUNT; ++i)
    {
        if (!slaveReady[i])
        {
            FD_SET(fileno(readFiles[i]), readFds);
            if (fileno(readFiles[i]) >= nfds)
                nfds = fileno(readFiles[i]);
        }
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

int lenstrcpy(char dest[], const char source[])
{
    int i = 0;
    while (source[i] != '\0')
    {
        dest[i] = source[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}
