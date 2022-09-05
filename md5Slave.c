#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define BLOCK 20

int main(int argc, char* argv[])
{
    int c = 0;
    int i = 0, size = 0;
    char* filename = malloc(BLOCK);
    if(filename==NULL)
    {
        perror("Error in allocating memory for filename"); //nose si malloc hace lo de errno
        exit(1);
    }
    //DEBUG
    fprintf(stderr,"DEBUG SLAVE: Slave created\n");
    //-----
    while((c = getchar()) != EOF)
    {
        //DEBUG
        fprintf(stderr,"DEBUG SLAVE: Read '%c'\n", c);
        //-----
        filename[i++] = c;
        if(size<i) size = i;
        if(size%BLOCK==0 && size>0)
        {
            filename = realloc(filename, size + BLOCK);
        }
        if(c==0)
        {
            //DEBUG
            fprintf(stderr,"DEBUG SLAVE: Received from master: %s\n", filename);
            //-----
            //terminó de leer el nombre del archivo, en file name tenemos el nombre con un \0 al final
            int pid = fork();
            if(pid==0)
            {
                //estamos en el hijo
                char command[size + BLOCK];
                sprintf(command, "md5sum %s", filename);
                if(system(command)!=0)
                {
                    perror("Error occurred while creating or executing md5sum process");
                    exit(1);
                }
                //el md5sum hereda los filedescriptors de este slave, por lo tanto cuando escriba
                //en lo que piensa que es stdout, se estará comunicando con el master directamente

                return 0;
            }
            if(pid==-1)
            {
                perror("Error in creating md5sum process");
                exit(1);
            }
            //estamos en el padre
            int status = 0;
            if(waitpid(pid, &status, WUNTRACED | WCONTINUED)==-1) // esperamos a que termine el md5sum
            {
                perror("Error in waiting for md5sum to end");
                exit(1);
            }
            if(status!=0)
            {
                perror("Error occurred in md5sum process");
                exit(1);
            }
            //el md5sum ya le mandó la rta por el pipe al master,
            //por lo que ya se enteró de que terminamos con este archivo
            i=0; //reseteamos el string de filename, para empezar devuelta
        }
    }
    //DEBUG
    fprintf(stderr,"DEBUG SLAVE: Found EOF (pipe closed)\n");
    //-----
    //cuando c==EOF, es porque el master cerró el pipe => terminamos
    free(filename);
    return 0;
}

