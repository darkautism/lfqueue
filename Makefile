
.PHONY: clean

main: clean
	gcc -std=c99 -g -O0 main.c lfq.c -o lfqtest -lpthread
	
clean:
	rm -rf *.o lfqtest