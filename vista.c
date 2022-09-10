// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#define _BSD_SOURCE
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <semaphore.h>
#include <dirent.h>
#include "shm_manager.h"

#define FREE_NAMES(x, name1, name2) if(x){ free(name1);free(name2);}

#define BUFFER_SIZE (NAME_MAX+2*32)

#define PIPE_WAS_USED 1
#define RUN_FROM_CONSOLE 5

int informationInCaseOfPipe(char **pshmName, ssize_t *pshmSize, char **psemaphoreName, int *pinitialSemaphoreValue);


void closeAllThings(unsigned char pipeWasUsed, char *shmName, char *semaphoreName, int semaphoreOpened,
                    ShmManagerADT shmManagerAdt);

int main(int argc, char *argv[])
{
    char *shmName = NULL;
    ssize_t shmSize;
    char *semaphoreName = NULL;
    int initialSemaphoreValue;
    unsigned char pipeWasUsed = 0;

    if (argc == PIPE_WAS_USED)
    {
        //Esta función leerá de stdin los parametros necesarios
        if (informationInCaseOfPipe(&shmName, &shmSize, &semaphoreName, &initialSemaphoreValue) == -1)
        {
            closeAllThings(1, shmName, semaphoreName, 0, NULL);
            exit(1);
        }
        pipeWasUsed = 1;
    } else if (argc == RUN_FROM_CONSOLE)
    {
        //si se pasaron los parametros a mano, los leemos
        shmName = argv[1];

        char *endPtr;
        shmSize = strtol(argv[2], &endPtr, 10);
        if ((endPtr == argv[2]) || (*endPtr != '\0'))
        {
            fprintf(stderr, "Error in entering of parameters. 2nd argument must be integer\n");
            exit(1);
        }

        semaphoreName = argv[3];

        initialSemaphoreValue = (int) strtol(argv[4], &endPtr, 10);
        if ((endPtr == argv[4]) || (*endPtr != '\0'))
        {
            fprintf(stderr, "Error in entering of parameters. 4th argument must be integer\n");
            exit(1);
        }

    } else
    {
        fprintf(stderr, "Error in entering of parameters\n");
        exit(1);
    }

    //Nos conectamos a la shared memory
    ShmManagerADT shmManagerAdt;
    if ((shmManagerAdt = newSharedMemoryManager(shmName, shmSize)) == NULL)
    {
        FREE_NAMES(pipeWasUsed, shmName, semaphoreName)
        exit(1);
    }
    if (connectToSharedMemory(shmManagerAdt) == -1)
    {
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 0, shmManagerAdt);
        exit(1);
    }

    //Nos conectamos al semáforo
    sem_t *semVistaReadyToRead = sem_open(semaphoreName, O_CREAT, O_CREAT | O_RDWR, initialSemaphoreValue);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of Semaphore");
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 0, shmManagerAdt);
        exit(1);
    }

    //Leemos los mensajes de la shared memory hasta que se nos indique que fue el último
    char buffer[BUFFER_SIZE];
    int lastMessage = 0;
    while (!lastMessage)
    {
        if (sem_wait(semVistaReadyToRead) == -1)
        {
            perror("Error in waiting for semaphore");
            closeAllThings(pipeWasUsed, shmName, semaphoreName, 1, shmManagerAdt);
            exit(1);
        }
        if (readMessage(shmManagerAdt, buffer, BUFFER_SIZE, &lastMessage) < 0)
        {
            closeAllThings(pipeWasUsed, shmName, semaphoreName, 1, shmManagerAdt);
            exit(1);
        }
        printf("%s\n", buffer);
    }

    //Cuando ya leímos hasta el último mensaje, terminamos
    closeAllThings(pipeWasUsed, shmName, semaphoreName, 1, shmManagerAdt);
    return 0;
}

void closeAllThings(unsigned char pipeWasUsed, char *shmName, char *semaphoreName, int semaphoreOpened,
                    ShmManagerADT shmManagerAdt)
{
    if (shmManagerAdt != NULL)
    {
        if (destroySharedMemory(shmManagerAdt) < 0)
        {
            exit(1);
        }
        freeSharedMemoryManager(shmManagerAdt);
    }

    if (semaphoreOpened && sem_unlink(semaphoreName))
    {
        perror("Error in unlinking semaphore");
        exit(1);
    }

    FREE_NAMES(pipeWasUsed, shmName, semaphoreName)

}


int informationInCaseOfPipe(char **pshmName, ssize_t *pshmSize, char **psemaphoreName, int *pinitialSemaphoreValue)
{
    size_t len;
    char *strings[4] = {NULL, NULL, NULL, NULL};

    for (int i = 0; i < 4; i++)
    {
        len = 0;
        ssize_t length = getline(&(strings[i]), &len, stdin);
        if (length == -1)
        {
            perror("Error assigning parameters value");
            return -1;
        }
        strings[i][length - 1] = '\0';   // para que no termine con '\n'
    }
    *pshmName = strings[0];

    char *endPtr;

    *pshmSize = strtol(strings[1], &endPtr, 10);
    if ((endPtr == strings[1]) || (*endPtr != '\0'))
    {
        fprintf(stderr, "Error in reading parameters from stdin. 2nd argument must be integer\n");
        return -1;
    }
    free(strings[1]);

    *psemaphoreName = strings[2];

    *pinitialSemaphoreValue = (int) strtol(strings[3], &endPtr, 10);
    if ((endPtr == strings[3]) || (*endPtr != '\0'))
    {
        fprintf(stderr, "Error in reading parameters from stdin. 4th argument must be integer\n");
        return -1;
    }
    free(strings[3]);
    return 0;
}

