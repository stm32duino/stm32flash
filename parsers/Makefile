all:
	gcc -g -Wall -c -I../ binary.c hex.c
	ar r parsers.a        binary.o hex.o

clean:
	rm -f *.o parsers.a
