CC = gcc
override CFLAGS += -Wall -pedantic

server: server.o defines.h ParseUtils.o ion.o
	$(CC) $(CFLAGS) -pthread $^ -o $@

server.o: server.c server.h

client: client.o ClientAPI.o timespecUtils.o ParseUtils.o defines.h
	$(CC) $(CFLAGS) $^ -o $@

client.o: client.c client.h

ClientAPI.o: ClientAPI.c ClientAPI.h

timespecUtils.o: timespecUtils.c timespecUtils.h

ParseUtils.o: ParseUtils.c ParseUtils.h

ion.o: ion.c ion.h

.PHONY: clean

clean:
	rm server client *.o
