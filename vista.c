#define _BSD_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <semaphore.h>


//deberían compartir un .h con estas definiciones
#define SHM_NAME "/oursharedmemory" 
#define SHM_SIZE 1024
#define SEMAPHORE_NAME "/mysemaphore"
#define INITIAL_SEMAPHORE_VALUE 0

int main(int argc, char* argv[])
{
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_RDWR, 0))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptoread = (char*) addr_mapped;
    
    // hay que usar semaforos para leer y escribir
    // creo que es mejor usar los unamed

    sem_t * semVistaReadyToRead = sem_open(SEMAPHORE_NAME,O_CREAT,O_CREAT|O_RDWR,INITIAL_SEMAPHORE_VALUE);
    if(semVistaReadyToRead==SEM_FAILED){
        perror("Error with Vista's opening of Semaphore");
        exit(1);
    }
    // usamos sem_open sem_post, sem_wait, sem_close, sem_unlink

    while(1)
    {
        sem_wait(semVistaReadyToRead);
        ptoread+=(printf("%s\n",ptoread)+1);
    }

}