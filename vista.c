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

#define BUFFER_SIZE 2*NAME_MAX

#define PIPE_WAS_USED 1
#define RUN_FROM_CONSOLE 5

void informationInCaseOfPipe(char **shmName, int *shmSize, char **semaphoreName, int *initialSemaphoreValue);

void checkErrorGetline(int length);

int main(int argc, char *argv[])
{

    char *shmName = NULL;
    int shmSize;
    char *semaphoreName = NULL;
    int initialSemaphoreValue;
    unsigned char pipeWasUsed = 0;

    if (argc == PIPE_WAS_USED)
    {
        informationInCaseOfPipe(&shmName, &shmSize, &semaphoreName, &initialSemaphoreValue);
        pipeWasUsed = 1;
    } else if (argc == RUN_FROM_CONSOLE)
    {
        shmName = argv[1];
        shmSize = atoi(argv[2]);
        semaphoreName = argv[3];
        initialSemaphoreValue = atoi(argv[4]);
    } else
    {
        printf("Error in entering of parameters\n");
        exit(1);
    }

    shmManagerADT shmManagerAdt = newSharedMemoryManager(shmName, shmSize);
    connectToSharedMemory(shmManagerAdt);

    sem_t *semVistaReadyToRead = sem_open(semaphoreName, O_CREAT, O_CREAT | O_RDWR, initialSemaphoreValue);
    if (semVistaReadyToRead == SEM_FAILED)
    {
        perror("Error with Vista's opening of Semaphore");
        exit(1);
    }

    char buffer[BUFFER_SIZE];
    int lastMessage = 0;
    while (!lastMessage)
    {
        sem_wait(semVistaReadyToRead);
        lastMessage = readMessage(shmManagerAdt, buffer, BUFFER_SIZE);
        printf("%s\n", buffer);
    }

    destroySharedMemory(shmManagerAdt);
    freeSharedMemoryManager(shmManagerAdt);

    if (sem_unlink(semaphoreName))
    {
        perror("Error in unlinking semaphore");
        exit(1);
    }

    if (pipeWasUsed)
    {
        free(shmName);
        free(semaphoreName);
    }
    return 0;
}


void informationInCaseOfPipe(char **pshmName, int *pshmSize, char **psemaphoreName, int *pinitialSemaphoreValue)
{
    size_t len = 0;
    int length;
    char *auxiliarStringForInts = NULL;

    length = getline(pshmName, &len, stdin);
    checkErrorGetline(length);
    (*pshmName)[length - 1] = '\0';   // para que no termine con '\n'

    len = 0;
    length = getline(&auxiliarStringForInts, &len, stdin);
    checkErrorGetline(length);
    *pshmSize = atoi(auxiliarStringForInts);
    free(auxiliarStringForInts);
    auxiliarStringForInts = NULL;

    len = 0;
    length = getline(psemaphoreName, &len, stdin);
    checkErrorGetline(length);
    (*psemaphoreName)[length - 1] = '\0';

    len = 0;
    length = getline(&auxiliarStringForInts, &len, stdin);
    checkErrorGetline(length);
    *pinitialSemaphoreValue = atoi(auxiliarStringForInts);
    free(auxiliarStringForInts);
}

void checkErrorGetline(int length)
{
    if (length == -1)
    {
        perror("Error assigning parameters value");
        exit(1);
    }
}
