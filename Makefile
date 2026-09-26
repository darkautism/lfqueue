CC ?= gcc
CFLAGS ?= -std=gnu99 -O3 -Wall -Wextra -Wpedantic -g
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lpthread

TEST_BINS = bin/test_edge_cases bin/test_p1c1 bin/test_p4c4 bin/test_p100c10 bin/test_p10c100 bin/test_aba

.PHONY: all test clean test-sanitize test-tsan

all: liblfq.a liblfq.so.1.0.0 $(TEST_BINS) bin/example

bin:
	mkdir -p bin

lfq.o: lfq.c lfq.h cross-platform.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c lfq.c -o $@

liblfq.a: lfq.o
	ar rcs $@ $<

liblfq.so.1.0.0: lfq.o
	$(CC) $(LDFLAGS) -shared -o $@ $<

bin/example: example.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) example.c lfq.c -o $@ $(LDLIBS)

bin/test_edge_cases: test_edge_cases.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_edge_cases.c lfq.c -o $@ $(LDLIBS)

bin/test_p1c1: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=1 -D MAX_CONSUMER=1

bin/test_p4c4: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=4 -D MAX_CONSUMER=4

bin/test_p100c10: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=100 -D MAX_CONSUMER=10 -D ITEMS_PER_PRODUCER=20000

bin/test_p10c100: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=10 -D MAX_CONSUMER=100 -D ITEMS_PER_PRODUCER=20000

bin/test_aba: test_aba.c lfq.c lfq.h cross-platform.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test_aba.c lfq.c -o $@ $(LDLIBS)

test: $(TEST_BINS)
	./bin/test_edge_cases
	./bin/test_p1c1
	./bin/test_p4c4
	./bin/test_p100c10
	./bin/test_p10c100
	./bin/test_aba

bin/test_edge_asan: test_edge_cases.c lfq.c lfq.h cross-platform.h | bin
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -fno-omit-frame-pointer -fsanitize=address,undefined test_edge_cases.c lfq.c -o $@ $(LDLIBS)

bin/test_mt_asan: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -fno-omit-frame-pointer -fsanitize=address,undefined test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=8 -D MAX_CONSUMER=8 -D ITEMS_PER_PRODUCER=5000

bin/test_aba_asan: test_aba.c lfq.c lfq.h cross-platform.h | bin
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -fno-omit-frame-pointer -fsanitize=address,undefined test_aba.c lfq.c -o $@ $(LDLIBS) -D ABA_THREADS=8 -D ABA_ITERATIONS=5000

test-sanitize: bin/test_edge_asan bin/test_mt_asan bin/test_aba_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./bin/test_edge_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./bin/test_mt_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./bin/test_aba_asan

bin/test_mt_tsan: test_multithread.c lfq.c lfq.h cross-platform.h | bin
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -fno-omit-frame-pointer -fsanitize=thread test_multithread.c lfq.c -o $@ $(LDLIBS) -D MAX_PRODUCER=8 -D MAX_CONSUMER=8 -D ITEMS_PER_PRODUCER=5000

bin/test_aba_tsan: test_aba.c lfq.c lfq.h cross-platform.h | bin
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -fno-omit-frame-pointer -fsanitize=thread test_aba.c lfq.c -o $@ $(LDLIBS) -D ABA_THREADS=8 -D ABA_ITERATIONS=5000

test-tsan: bin/test_mt_tsan bin/test_aba_tsan
	TSAN_OPTIONS=halt_on_error=1 ./bin/test_mt_tsan
	TSAN_OPTIONS=halt_on_error=1 ./bin/test_aba_tsan

clean:
	rm -f *.o liblfq.a liblfq.so.1.0.0
	rm -f bin/test_* bin/example
