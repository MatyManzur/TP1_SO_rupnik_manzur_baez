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


#define PIPE_WAS_USED 1
#define RUN_FROM_CONSOLE 5

void informationInCaseOfPipe(char ** shmName, int * shmSize, char ** semaphoreName, int * initialSemaphoreValue);
void checkErrorGetline(int length);
void * openSharedMemory(int * fdsharedmem, char * shmName, int shmSize);

int main(int argc, char* argv[])
{

    char * shmName=NULL;
    int shmSize;
    char * semaphoreName=NULL;
    int initialSemaphoreValue;
    unsigned char pipeWasUsed=0;

    if(argc==PIPE_WAS_USED)
    {
        informationInCaseOfPipe(&shmName, &shmSize, &semaphoreName, &initialSemaphoreValue);
        pipeWasUsed=1;
    }
    else if(argc==RUN_FROM_CONSOLE)
    {
        shmName = argv[1];
        shmSize = atoi(argv[2]);
        semaphoreName = argv[3];
        initialSemaphoreValue = atoi(argv[4]);
    }
    else
    {
        printf("Error in entering of parameters\n");
        exit(1);
    }

    int fdsharedmem;
    void * addr_mapped = openSharedMemory(&fdsharedmem,shmName,shmSize);
    char* ptoread = (char*) addr_mapped;

    sem_t * semVistaReadyToRead = sem_open(semaphoreName,O_CREAT,O_CREAT|O_RDWR,initialSemaphoreValue);
    if(semVistaReadyToRead==SEM_FAILED){
        perror("Error with Vista's opening of Semaphore");
        exit(1);
    }

    sem_wait(semVistaReadyToRead);
    char charShowingContinuation=*ptoread;
    while(*ptoread == charShowingContinuation)
    {
        ptoread++;
        sem_wait(semVistaReadyToRead);
        ptoread+=printf("%s\n",ptoread);
    }


    if(sem_close(semVistaReadyToRead)==-1)
    {
        perror("Error closing semaphore");
    }
    if(sem_unlink(semaphoreName)){
        perror("Error destroying semaphore");
        exit(1);
    }
    if(munmap(addr_mapped,shmSize)==-1){
        perror("Error in destroying shared memory");
        exit(1);
    }
    if(shm_unlink(shmName)==-1){
        perror("Error unlinking shared memory object");
        exit(1);
    }
    if(pipeWasUsed){
        free(shmName);
        free(semaphoreName);
    }
    return close(fdsharedmem); 
}


void informationInCaseOfPipe(char ** pshmName, int * pshmSize, char ** psemaphoreName, int * pinitialSemaphoreValue){
    size_t len=0;
    int length;
    char * auxiliarStringForInts=NULL;

    length=getline(pshmName, &len,stdin);
    checkErrorGetline(length);
    (*pshmName)[length-1] = '\0';   // para que no termine con '\n'

    len=0;
    length=getline(&auxiliarStringForInts, &len,stdin);
    checkErrorGetline(length);
    *pshmSize = atoi(auxiliarStringForInts);
    free(auxiliarStringForInts);
    auxiliarStringForInts=NULL;

    len=0;
    length=getline(psemaphoreName, &len,stdin);
    checkErrorGetline(length);
    (*psemaphoreName)[length-1] = '\0';

    len=0;
    length=getline(&auxiliarStringForInts, &len,stdin);
    checkErrorGetline(length);
    *pinitialSemaphoreValue = atoi(auxiliarStringForInts);
    free(auxiliarStringForInts);
}

void checkErrorGetline(int length){
    if(length==-1){
        perror("Error assigning parameters value");
        exit(1);
    }
}

void * openSharedMemory(int * fdsharedmem, char * shmName, int shmSize){
    void * addr_mapped;
    if(((*fdsharedmem)=shm_open(shmName,O_RDWR, 0))==-1)
        perror("Error opening Shared Memory");

    if((addr_mapped=mmap(NULL,shmSize,PROT_READ|PROT_WRITE, MAP_SHARED,*fdsharedmem,0))==MAP_FAILED)
        perror("Problem mapping shared memory");

    return addr_mapped;
}
