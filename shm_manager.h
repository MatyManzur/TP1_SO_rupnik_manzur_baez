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

typedef struct shmManagerCDT* shmManagerADT;

shmManagerADT newSharedMemoryManager(char* shmName, ssize_t shmSize);
void freeSharedMemoryManager(shmManagerADT shmManagerAdt);

int createSharedMemory(shmManagerADT shmManagerAdt);
int connectToSharedMemory(shmManagerADT shmManagerAdt);
int disconnectFromSharedMemory(shmManagerADT shmManagerAdt);
int destroySharedMemory(shmManagerADT shmManagerAdt);

int writeMessage(shmManagerADT shmManagerAdt, char* message, int last);
int readMessage(shmManagerADT shmManagerAdt, char* buff, ssize_t length);

#endif