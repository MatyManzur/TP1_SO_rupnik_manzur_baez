#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <string.h>

#define MD5_COMMAND "md5sum "
#define MD5_COMMAND_LENGTH 7

int main(int argc, char* argv[])
{
    char c = 0;
    int i = 0;
    char filename[NAME_MAX];
    //DEBUG
        fprintf(stderr,"DEBUG SLAVE: Slave created\n");
    //-----
    ssize_t readReturnValue;
    while((readReturnValue = read(STDIN_FILENO,&c,1)) > 0) //si devuelve 0 es porque encontro un EOF
    {
        filename[i++] = c;
        if(i>=NAME_MAX)
        {
            fprintf(stderr,"File name too long! : %s\n", filename);
            exit(1);
        }
        if(c=='\0')
        {
            //DEBUG
            fprintf(stderr,"DEBUG SLAVE: Received %s from master\n", filename);
            //-----
            //terminó de leer el nombre del archivo, en file name tenemos el nombre con un \0 al final
            int pid = fork();
            if(pid==0)
            {
                //estamos en el hijo
                char command[NAME_MAX + MD5_COMMAND_LENGTH] = MD5_COMMAND;
                strncat(command, filename, NAME_MAX + MD5_COMMAND_LENGTH);
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
            //estamos en el padre, esperamos a que termine md5sum
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
    if(readReturnValue == -1) //si read dio error
    {
        perror("Error in reading from pipe");
        exit(1);
    }
    //si estamos acá, es porque encontró un EOF (el master cerro el pipe)
    //DEBUG
        fprintf(stderr,"DEBUG SLAVE: Found EOF (pipe closed)\n");
    //-----
    return 0;
}

