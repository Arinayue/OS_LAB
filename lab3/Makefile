CC = gcc
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = -lrt -pthread

all: parent child

parent: parent.c
	$(CC) $(CFLAGS) -o parent parent.c $(LDFLAGS)

child: child.c
	$(CC) $(CFLAGS) -o child child.c $(LDFLAGS)

clean:
	rm -f parent child out1.txt out2.txt
