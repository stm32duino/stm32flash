all:
	gcc -o stm32flash \
		main.c \
		serial_common.c \
		serial_linux.c \
		-Wall
