all:
	gcc -o stm32flash \
		main.c \
		utils.c \
		stm32.c \
		serial_common.c \
		serial_linux.c \
		-Wall
