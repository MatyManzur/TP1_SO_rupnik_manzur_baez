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

#define BUFFER_SIZE (NAME_MAX+2*32)

#define PIPE_WAS_USED 1
#define RUN_FROM_CONSOLE 5

int informationInCaseOfPipe(char **shmName, int *shmSize, char **semaphoreName, int *initialSemaphoreValue);


void closeAllThings(unsigned char pipeWasUsed, char *shmName, char *semaphoreName, int semaphoreOpened,
                    ShmManagerADT shmManagerAdt);

int main(int argc, char *argv[])
{
    char *shmName = NULL;
    int shmSize;
    char *semaphoreName = NULL;
    int initialSemaphoreValue;
    unsigned char pipeWasUsed = 0;

    if (argc == PIPE_WAS_USED)
    {
        if (informationInCaseOfPipe(&shmName, &shmSize, &semaphoreName, &initialSemaphoreValue) == -1)
        {
            closeAllThings(1, shmName, semaphoreName, 0, NULL);
            exit(1);
        }
        pipeWasUsed = 1;
    } else if (argc == RUN_FROM_CONSOLE)
    {
        shmName = argv[1];
        shmSize = atoi(argv[2]);
        semaphoreName = argv[3];
        initialSemaphoreValue = atoi(argv[4]);
    } else
    {
        fprintf(stderr, "Error in entering of parameters\n");
        exit(1);
    }

    ShmManagerADT shmManagerAdt;
    if (newSharedMemoryManager(shmName, shmSize) == NULL)
    {
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 0, NULL);
        exit(1);
    }
    if (connectToSharedMemory(shmManagerAdt) == -1)
    {
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 0, shmManagerAdt);
        exit(1);
    }

    sem_t *semVistaReadyToRead = sem_open(semaphoreName, O_CREAT, O_CREAT | O_RDWR, initialSemaphoreValue);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of Semaphore");
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 0, shmManagerAdt);
        exit(1);
    }

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
        lastMessage = readMessage(shmManagerAdt, buffer, BUFFER_SIZE);
        printf("%s\n", buffer);
    }
    if (lastMessage < 0)
    {
        closeAllThings(pipeWasUsed, shmName, semaphoreName, 1, shmManagerAdt);
        exit(1);
    }

    closeAllThings(pipeWasUsed, shmName, semaphoreName, shmManagerAdt);
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

    if (pipeWasUsed)
    {
        free(shmName);
        free(semaphoreName);
    }
}


int informationInCaseOfPipe(char **pshmName, int *pshmSize, char **psemaphoreName, int *pinitialSemaphoreValue)
{
    size_t len;
    ssize_t length;
    char *strings[4] = {*pshmName, NULL, *psemaphoreName, NULL};

    for (int i = 0; i < 4; i++)
    {
        len = 0;
        length = getline(&(strings[i]), &len, stdin);
        if (length == -1)
        {
            perror("Error assigning parameters value");
            return -1;
        }
        strings[i][length - 1] = '\0';   // para que no termine con '\n'
    }

    *pshmSize = atoi(strings[1]);
    free(strings[1]);

    *pinitialSemaphoreValue = atoi(strings[3]);
    free(strings[3]);

    return 0;
}

