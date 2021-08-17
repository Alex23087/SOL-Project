CC = gcc
override CFLAGS += -Wall -pedantic

server: server.o ParseUtils.o ion.o queue.o
	$(CC) $(CFLAGS) -pthread $^ -o $@

server.o: server.c server.h

client: client.o ClientAPI.o timespecUtils.o ParseUtils.o
	$(CC) $(CFLAGS) $^ -o $@

client.o: client.c client.h

ClientAPI.o: ClientAPI.c ClientAPI.h

timespecUtils.o: timespecUtils.c timespecUtils.h

ParseUtils.o: ParseUtils.c ParseUtils.h

ion.o: ion.c ion.h

queue.o: queue.c queue.h

.PHONY: clean cleanall

cleanall: clean
	rm server client

clean:
	rm *.o