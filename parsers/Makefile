
CFLAGS += -Wall -g

parsers.a: binary.o hex.o
	$(AR) r parsers.a binary.o hex.o

clean:
	rm -f *.o parsers.a
