all:
	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Wl,-static -Wl,--gc-sections -nostartfiles \
		-o stmreset.elf \
		crt0.S \
		linker.ld \
		stmreset.c
	arm-none-eabi-objcopy -O binary stmreset.elf stmreset.bin

clean:
	rm -f stmreset.elf
	rm -f stmreset.bin
