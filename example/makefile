CC=gcc
CFLAGS=-Wall -g -I../include

all: hello controller

hello: hello.o
	$(CC) $(CFLAGS) -o hello hello.o

controller: controller.o gdbot.o
	$(CC) $(CFLAGS) -o controller controller.o gdbot.o

hello.o: hello.c
	$(CC) $(CFLAGS) -c hello.c

controller.o: controller.c
	$(CC) $(CFLAGS) -c controller.c

gdbot.o: ../source/gdbot.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o hello controller
