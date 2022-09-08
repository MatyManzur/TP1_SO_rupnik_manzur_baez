#ifndef _SHM_MANAGER_H
#define _SHM_MANAGER_H

#define _BSD_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>


#define CHARACTER_SHOWING_CONTINUATION '\n'

typedef struct shmManagerCDT *ShmManagerADT;

ShmManagerADT newSharedMemoryManager(char *shmName, ssize_t shmSize);

void freeSharedMemoryManager(ShmManagerADT shmManagerAdt);

int createSharedMemory(ShmManagerADT shmManagerAdt);

int connectToSharedMemory(ShmManagerADT shmManagerAdt);

int disconnectFromSharedMemory(ShmManagerADT shmManagerAdt);

int destroySharedMemory(ShmManagerADT shmManagerAdt);

int writeMessage(ShmManagerADT shmManagerAdt, char *message, int last);

int readMessage(ShmManagerADT shmManagerAdt, char *buff, ssize_t length);

#endif