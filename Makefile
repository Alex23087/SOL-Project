CC = gcc
override CFLAGS += -Wall -pedantic
VPATH=./src/

all: client server

server: mkbuilddir build/server.o build/ParseUtils.o build/ion.o build/queue.o build/FileCachingProtocol.o build/FileCache.o
	$(CC) $(CFLAGS) -pthread $(filter-out $<,$^) -o $@

build/server.o: src/server.c
	$(CC) $(CFLAGS) $^ -c -o $@

client: mkbuilddir ./build/client.o build/ClientAPI.o build/timespecUtils.o build/ParseUtils.o build/ion.o build/FileCachingProtocol.o build/queue.o
	$(CC) $(CFLAGS) $(filter-out $<,$^) -o $@

build/client.o: src/client.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/ClientAPI.o: src/lib/ClientAPI.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/timespecUtils.o: src/lib/TimespecUtils.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/ParseUtils.o: src/lib/ParseUtils.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/ion.o: src/lib/ion.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/queue.o: src/lib/Queue.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/FileCachingProtocol.o: src/lib/FileCachingProtocol.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/FileCache.o: src/lib/FileCache.c
	$(CC) $(CFLAGS) $^ -c -o $@

.PHONY: clean cleanall mkbuilddir

cleanall: clean
	rm server client

clean:
	rm -rf build

mkbuilddir:
	mkdir -p build