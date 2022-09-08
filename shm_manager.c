#include "shm_manager.h"

typedef struct shmManagerCDT
{
    int fdsharedmem;
    char *shmName;
    ssize_t shmSize;
    char *initialMemPtr;
    char *memPtr;
} shmManagerCDT;

ShmManagerADT newSharedMemoryManager(char *shmName, ssize_t shmSize)
{
    ShmManagerADT adt;
    if((adt = calloc(1, sizeof(struct shmManagerCDT)))==NULL)
    {
        perror("Error in allocating memory for SharedMemoryManager");
        return NULL;
    }
    adt->fdsharedmem = -1;
    adt->shmName = shmName;
    adt->shmSize = shmSize;
    return adt;
}

int lenstrcpy(char dest[], const char source[]);

int checkIfConnected(ShmManagerADT shmManagerAdt);

void freeSharedMemoryManager(ShmManagerADT shmManagerAdt)
{
    free(shmManagerAdt);
}

int createSharedMemory(ShmManagerADT shmManagerAdt)
{
    if ((shmManagerAdt->fdsharedmem = shm_open(shmManagerAdt->shmName, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1)
    {
        perror("Error opening Shared Memory");
        return -1;
    }

    if (ftruncate(shmManagerAdt->fdsharedmem, shmManagerAdt->shmSize) == -1)
    {
        perror("Error trying to ftruncate");
        return -1;
    }

    if ((shmManagerAdt->initialMemPtr = mmap(NULL, shmManagerAdt->shmSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                                             shmManagerAdt->fdsharedmem, 0)) == MAP_FAILED)
    {
        perror("Problem mapping shared memory");
        return -1;
    }
    shmManagerAdt->memPtr = shmManagerAdt->initialMemPtr;
    return 0;
}

int connectToSharedMemory(ShmManagerADT shmManagerAdt)
{
    if ((shmManagerAdt->fdsharedmem = shm_open(shmManagerAdt->shmName, O_RDWR, 0)) == -1)
    {
        perror("Error opening Shared Memory");
        return -1;
    }

    if ((shmManagerAdt->initialMemPtr = mmap(NULL, shmManagerAdt->shmSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                                             shmManagerAdt->fdsharedmem, 0)) == MAP_FAILED)
    {
        perror("Problem mapping shared memory");
        return -1;
    }
    shmManagerAdt->memPtr = shmManagerAdt->initialMemPtr;
    return 0;
}

int disconnectFromSharedMemory(ShmManagerADT shmManagerAdt)
{
    if (!checkIfConnected(shmManagerAdt))
    {
        fprintf(stderr, "Must be connected to a shared memory!");
        return -2;
    }
    if (munmap(shmManagerAdt->initialMemPtr, shmManagerAdt->shmSize) == -1)
    {
        perror("Error in unmaping shared memory");
        return -1;
    }
    if (close(shmManagerAdt->fdsharedmem))
    {
        perror("Error in closing shared memory");
        return -1;
    }
    return 0;
}

int destroySharedMemory(ShmManagerADT shmManagerAdt)
{
    if (!checkIfConnected(shmManagerAdt))
    {
        fprintf(stderr, "Must be connected to a shared memory!");
        return -2;
    }
    if (munmap(shmManagerAdt->initialMemPtr, shmManagerAdt->shmSize) == -1)
    {
        perror("Error in unmaping shared memory");
        return -1;
    }
    if (shm_unlink(shmManagerAdt->shmName) == -1)
    {
        perror("Error unlinking shared memory object");
        return -1;
    }
    if (close(shmManagerAdt->fdsharedmem))
    {
        perror("Error in closing shared memory");
        return -1;
    }
    return 0;
}

int writeMessage(ShmManagerADT shmManagerAdt, char *message, int last)
{
    if (!checkIfConnected(shmManagerAdt))
    {
        fprintf(stderr, "Must be connected to a shared memory!");
        return -2;
    }
    shmManagerAdt->memPtr += (lenstrcpy(shmManagerAdt->memPtr, message) + 1);
    if (shmManagerAdt->memPtr > shmManagerAdt->initialMemPtr + shmManagerAdt->shmSize)
    {
        fprintf(stderr, "Exceeded shared memory size!");
        return -1;
    }
    if (!last)
    {
        *shmManagerAdt->memPtr = CHARACTER_SHOWING_CONTINUATION;
        shmManagerAdt->memPtr++;
    }
    return 0;
}

int readMessage(ShmManagerADT shmManagerAdt, char *buff, ssize_t length)
{
    if (!checkIfConnected(shmManagerAdt))
    {
        fprintf(stderr, "Must be connected to a shared memory!");
        return -2;
    }
    int snprintfReturnValue = snprintf(buff, length, "%s", shmManagerAdt->memPtr);
    if (snprintfReturnValue < 0)
    {
        perror("Error in reading from shm");
        return -1;
    }
    shmManagerAdt->memPtr += snprintfReturnValue + 1;
    if (*(shmManagerAdt->memPtr) != CHARACTER_SHOWING_CONTINUATION)
    {
        return 1;
    }
    return 0;
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

int checkIfConnected(ShmManagerADT shmManagerAdt)
{
    return shmManagerAdt->fdsharedmem >= 0;
}