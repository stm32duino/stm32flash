all:
	$(MAKE) -C parsers
	gcc -g -o stm32flash -I./ \
		main.c \
		utils.c \
		stm32.c \
		serial_common.c \
		serial_linux.c \
		parsers/parsers.a \
		stm32/stmreset_binary.c \
		-Wall

clean:
	$(MAKE) -C parsers clean
	rm -f stm32flash
