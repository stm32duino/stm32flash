
CFLAGS = -Wall -g

all: stm32flash

serial_platform.o: serial_posix.c serial_w32.c

OBJS = main.o utils.o stm32.o serial_common.o serial_platform.o \
       stm32/stmreset_binary.o parsers/parsers.a

stm32/stmreset_binary.c:
	$(MAKE) -C stm32

parsers/parsers.a:
	$(MAKE) -C parsers
	
stm32flash: $(OBJS)
	$(CC) -o stm32flash $(OBJS)

clean:
	rm -f $(OBJS) stm32flash
	$(MAKE) -C parsers clean

install: all
	cp stm32flash /usr/local/bin
