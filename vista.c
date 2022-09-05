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


int main(int argc, char* argv[])
{
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_RDWR, 0))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptoread = (char*) addr_mapped;
    ptoread+=16;
    // hay que usar semaforos para leer y escribir
    // creo que es mejor usar los unamed

    sem_t *semaphore = (sem_t *) addr_mapped;
    
    // usamos sem_init sem_post, sem_wait, sem_destroy
    // When the semaphore is no longer required, and before the  memory
    // in which it is located is deallocated, the semaphore should be destroyed using sem_destroy(3).

    while(1)
    {
        sem_wait(semaphore);
        printf("%s\n",ptoread);
        sem_post(semaphore);
    }

}