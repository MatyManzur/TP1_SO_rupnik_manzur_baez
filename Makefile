MYCOMPILER := gcc
FLAGS_COMPILER := -g -std=c99 -Wall -lrt -pthread
VISTA := vista.c -o vista
MD5 := md5.c -o md5
MD5SLAVE := md5Slave.c -o md5Slave
SHM_MANAGER := shm_manager.c

all:
	$(MYCOMPILER) $(VISTA) $(SHM_MANAGER) $(FLAGS_COMPILER)
	$(MYCOMPILER) $(MD5) $(SHM_MANAGER) $(FLAGS_COMPILER)
	$(MYCOMPILER) $(MD5SLAVE) $(FLAGS_COMPILER)

clean:
	rm vista
	rm md5
	rm md5Slave
	rm Resultados.txt
