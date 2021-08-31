CC = gcc
override CFLAGS += -Wall -pedantic --std=gnu99
MAKEFLAGS := --jobs=$(shell nproc)
.PHONY: all clean cleanall killserver intserver hupserver testlock testhangup test1 test2 test3 cleantestlock cleantesthangup cleantest1 cleantest2 cleantest3 morefiles rmmorefiles stats
SERVERDEPS = server FileCache FileCachingProtocol ion miniz ParseUtils Queue ServerLib TimespecUtils W2M
CLIENTDEPS = client ClientAPI FileCachingProtocol ion ParseUtils PathUtils Queue TimespecUtils



all: client server



server: build $(addprefix build/,$(addsuffix .o, $(SERVERDEPS)))
	$(CC) $(CFLAGS) -pthread $(filter-out $<,$^) -o $@

client: build $(addprefix build/,$(addsuffix .o, $(CLIENTDEPS)))
	$(CC) $(CFLAGS) $(filter-out $<,$^) -o $@



build:
	mkdir -p build

build/%.o: src/%.c
	$(CC) $(CFLAGS) $^ -c -o $@

build/%.o: src/lib/%.c
	$(CC) $(CFLAGS) $^ -c -o $@



cleanall: clean cleantest1 cleantest2 cleantest3 rmmorefiles
	rm -f server client /tmp/LSOfilestorage.sk /tmp/LSOfilestorage.log

clean:
	rm -rf build



killserver:
	ps -ao pid,command | grep -E "(valgrind)?server" | head -n1 | grep -Eo "[0-9]{3,6}" | xargs -n1 kill -SIGKILL

intserver:
	ps -ao pid,command | grep -E "(valgrind)?server" | head -n1 | grep -Eo "[0-9]{3,6}" | xargs -n1 kill -SIGINT

hupserver:
	ps -ao pid,command | grep -E "(valgrind)?server" | head -n1 | grep -Eo "[0-9]{3,6}" | xargs -n1 kill -SIGHUP



testlock: cleantest1 all
	(sleep 1 && chmod +x ./tests/lock/startClients.sh && ./tests/lock/startClients.sh) &
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server -c tests/test1/config.txt

cleantestlock: cleantest1

testhangup: cleantest1 all
	(sleep 1 && chmod +x ./tests/hangup/startClients.sh && ./tests/hangup/startClients.sh) &
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server -c tests/test1/config.txt

cleantesthangup: cleantest1

test1: cleantest1 all
	(mkdir -p ./tests/test1/tmp && sleep 1 && chmod +x ./tests/test1/startClients.sh && ./tests/test1/startClients.sh) &
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server -c tests/test1/config.txt

cleantest1:
	rm -rf ./tests/test1/tmp

test2: cleantest2 all
	(mkdir -p ./tests/test2/tmp && sleep 1 && chmod +x ./tests/test2/startClients.sh && ./tests/test2/startClients.sh) &
	./server -c tests/test2/config.txt

cleantest2:
	rm -rf ./tests/test2/tmp

test3: cleantest2 all
	(mkdir -p ./tests/test3/tmp && sleep 1 && chmod +x ./tests/test3/startClients.sh && ./tests/test3/startClients.sh) &
	./server -c tests/test3/config.txt

cleantest3:
	rm -rf ./tests/test3/tmp

morefiles:
	cp ./src/*.c ./src/lib/* ./src/include/* ./tests/cats/small/

rmmorefiles:
	rm -f ./tests/cats/small/*.c ./tests/cats/small/*.h

stats:
	chmod +x ./statistiche.sh && time ./statistiche.sh