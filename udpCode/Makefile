# udpCode makefile
# written by Hugh Smith - April 2017

CC = gcc
CFLAGS= -g

#append a 32 to executable name if on a 32 bit machine
FILE = 
ARCH = $(shell arch)
ifeq ("$(ARCH)", "i686")
	FILE = 32
endif

SRC = networks.c gethostbyname.c 

all:  udpClient$(FILE) udpServer$(FILE)

udpClient$(FILE): udpClient.c $(SRC)
	$(CC) $(CFLAGS) -o udpClient$(FILE) udpClient.c $(SRC)

udpServer$(FILE): udpServer.c $(SRC)
	$(CC) $(CFLAGS) -o udpServer$(FILE) udpServer.c  $(SRC)

cleano:
	rm -f *.o

clean:
	rm -f udpServer udpClient udpServer32 udpClient32 *.o

