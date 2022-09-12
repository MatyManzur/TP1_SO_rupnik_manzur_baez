CC = gcc
CFLAGS = -g -std=c99 -Wall
LIBS = -lrt -lpthread
DEPS = shm_manager.h
OBJS = md5.o shm_manager.o vista.o
EXECS = md5 vista md5Slave

all: $(EXECS)

%.o: %.c $(DEPS)
	$(CC) -c $< $(CFLAGS)

md5: md5.o shm_manager.o
	$(CC) $^ -o $@ $(LIBS)

vista: vista.o shm_manager.o
	$(CC) $^ -o $@ $(LIBS)

md5Slave:
	$(CC) md5Slave.c -o $@ $(CFLAGS)

.PHONY: clean

clean:
	rm -f $(OBJS) $(EXECS) Resultados.txt
