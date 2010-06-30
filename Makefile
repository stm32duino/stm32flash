all:
	gcc -o stm32flash main.c serial_linux.c -Wall
