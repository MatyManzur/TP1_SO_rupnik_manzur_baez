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
#define SEMPAHORE_NAME "/mysem"

int main(int argc, char* argv[])
{
    int fdsharedmem;
    void * addr_mapped;
    if((fdsharedmem=shm_open(SHM_NAME,O_RDWR, 0))==-1) perror("Error opening Shared Memory"); // no me acuerdo qué era el último argumento
    if((addr_mapped=mmap(NULL,SHM_SIZE,PROT_READ|PROT_WRITE, MAP_SHARED,fdsharedmem,0))==MAP_FAILED) perror("Problem mapping shared memory");
    char* ptoread = (char*) addr_mapped;

    // hay que usar semaforos para leer y escribir
    // creo que es mejor usar los Named porque no entendí cómo se inicializaban los otros :D (man sem_overview)

    sem_t *semaphore;
    if((semaphore=sem_open(SEMPAHORE_NAME,O_RDWR))==SEM_FAILED)
    {
        perror("Error initiating a semaphore");
        exit(1);
    }
    // usamos sem_post, sem_wait, sem_close y sem_unlink
    while(1)
    {
        sem_wait(semaphore);
        printf("%s\n",ptoread);
        sem_post(semaphore);
    }

}