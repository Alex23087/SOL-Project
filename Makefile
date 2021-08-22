CC = gcc
override CFLAGS += -Wall -pedantic
.PHONY: all clean cleanall mkbuilddir killserver hupserver test1 test2 test3


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



cleanall: clean
	rm server client

clean:
	rm -rf build

mkbuilddir:
	mkdir -p build


killserver:
	ps -o pid,command | grep -E "(valgrind)?server" | grep -Eo "[0-9]{3,6}" | xargs -n1 kill -SIGTERM

hupserver:
	ps -o pid,command | grep -E "(valgrind)?server" | grep -Eo "[0-9]{3,6}" | xargs -n1 kill -SIGHUP

test1:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./server -c tests/test1/config.txt&
	sleep 5&
	./tests/test1/startClients.sh