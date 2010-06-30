all:
	$(MAKE) -C parsers
	gcc -o stm32flash -I./ \
		main.c \
		utils.c \
		stm32.c \
		serial_common.c \
		serial_linux.c \
		parsers/*.o \
		-Wall
