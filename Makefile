
CC=gcc 
CFLAGS= -Wall -g -O2

.PHONY: all

all: mtask

mtask: mtask.o kernel.o
	$(CC) $(CFLAGS) -o $@ mtask.o kernel.o -lm

mtask.c: syscalls.h
kernel.c: syscalls.h



