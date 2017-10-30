
all : p1c1 p4c4 p100c10 p10c100 example

example: liblfq
	gcc -std=c99 -g example.c lfq.c -o bin/example -lpthread

p1c1: liblfq
	gcc -std=c99 -g test_multithread.c -o bin/test_p1c1 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=1 -D MAX_CONSUMER=1 
	
p4c4: liblfq
	gcc -std=c99 -g test_multithread.c -o bin/test_p4c4 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=4 -D MAX_CONSUMER=4

p100c10: liblfq
	gcc -std=c99 -g test_multithread.c -o bin/test_p100c10 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=100 -D MAX_CONSUMER=10
	
p10c100: liblfq
	gcc -std=c99 -g test_multithread.c -o bin/test_p10c100 -L. -Wl,-Bstatic -llfq -Wl,-Bdynamic -lpthread -D MAX_PRODUCER=10 -D MAX_CONSUMER=100
	
liblfq: lfq.c 
	@gcc -std=c99 -c -g lfq.c -lpthread
	@ar rcs liblfq.a lfq.o
	@gcc -std=c99 -fPIC -c lfq.c
	@gcc -shared -o liblfq.so.1.0.0 lfq.o
	
test:
	@bin/test_p1c1
	@bin/test_p4c4
	@bin/test_p100c10
	@bin/test_p10c100
	
clean:
	rm -rf *.o bin/* liblfq.so.1.0.0 liblfq.a
