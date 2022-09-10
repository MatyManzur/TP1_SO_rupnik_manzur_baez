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

//Crea una nueva instancia de SharedMemoryManager
//Devuelve un puntero a la misma, o NULL si hubo un error
ShmManagerADT newSharedMemoryManager(char *shmName, ssize_t shmSize);

//Todas las siguientes (excepto readMessage) devuelven 0 si no hubo errores, o un numero negativo si los hubo

//Libera el espacio de memoria que ocupaba el ADT
int freeSharedMemoryManager(ShmManagerADT shmManagerAdt);

//Crea una shared memory. No hay que llamar a connectToSharedMemory también
int createSharedMemory(ShmManagerADT shmManagerAdt);

//Se conecta a una shared memory creada por otro
int connectToSharedMemory(ShmManagerADT shmManagerAdt);

//Se desconecta de una shared memory, pero no la destruye
int disconnectFromSharedMemory(ShmManagerADT shmManagerAdt);

//Elimina por completo la shared memory. Ya nadie la podrá usar
int destroySharedMemory(ShmManagerADT shmManagerAdt);

//Escribe el mensaje indicado en la shared memory. Se indica por parámetro si es el último mensaje
int writeMessage(ShmManagerADT shmManagerAdt, char *message, int lastMessage);

//Lee de la shared memory el mensaje.
//Devuelve cuantos caracteres leyó. Si hubo un error devuelve un número negativo.
//Devuelve por parámetro si es mensaje leído es el último
int readMessage(ShmManagerADT shmManagerAdt, char *buff, ssize_t length, int *lastMessage);

#endif