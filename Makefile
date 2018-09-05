CC=gcc
CFLAGS=-std=gnu99 -O3 -Wall -Wextra -g
LDFLAGS=-g
LOADLIBS=-lpthread

all : bin/test_p1c1 bin/test_p4c4 bin/test_p100c10 bin/test_p10c100 bin/example

bin/example: example.c liblfq.so.1.0.0
	gcc $(CFLAGS) $(LDFLAGS) example.c lfq.c -o bin/example -lpthread

bin/test_p1c1: liblfq.so.1.0.0 test_multithread.c
	gcc $(CFLAGS) $(LDFLAGS) test_multithread.c -o bin/test_p1c1 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=1 -D MAX_CONSUMER=1

bin/test_p4c4: liblfq.so.1.0.0 test_multithread.c
	gcc $(CFLAGS) $(LDFLAGS) test_multithread.c -o bin/test_p4c4 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=4 -D MAX_CONSUMER=4

bin/test_p100c10: liblfq.so.1.0.0 test_multithread.c
	gcc $(CFLAGS) $(LDFLAGS) test_multithread.c -o bin/test_p100c10 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=100 -D MAX_CONSUMER=10

bin/test_p10c100: liblfq.so.1.0.0 test_multithread.c
	gcc $(CFLAGS) $(LDFLAGS) test_multithread.c -o bin/test_p10c100 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=10 -D MAX_CONSUMER=100

liblfq.so.1.0.0: lfq.c lfq.h cross-platform.h
	gcc $(CFLAGS) -c lfq.c   # -fno-pie for static linking?
	ar rcs liblfq.a lfq.o
	gcc $(CFLAGS) -fPIC -c lfq.c
	gcc $(LDFLAGS) -shared -o liblfq.so.1.0.0 lfq.o

test: bin/test_p1c1 bin/test_p4c4 bin/test_p100c10 bin/test_p10c100
	bin/test_p1c1
	bin/test_p4c4
	bin/test_p100c10
	bin/test_p10c100

clean:
	rm -rf *.o bin/* liblfq.so.1.0.0 liblfq.a
