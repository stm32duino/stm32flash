/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2010 Geoffrey McRae <geoff@spacevs.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "serial.h"
#include "stm32.h"

serial_t	*serial;
stm32_t		stm;
char		le; //true if cpu is little-endian

uint32_t swap_u32(const uint32_t v);
void     send_byte(const uint8_t byte);
char     read_byte();
char     send_command(uint8_t cmd);
char     init_stm32();
void     free_stm32();
char     erase_memory();
char     read_memory (uint32_t address, uint8_t data[], unsigned int len);
char     write_memory(uint32_t address, uint8_t data[], unsigned int len);

int main(int argc, char* argv[]) {
	int i;

	/* detect CPU endian */
	const uint32_t x = 0x12345678;
	le = ((unsigned char*)&x)[0] == 0x78;
	char *device = NULL;

	int		ret		= 1;
	serial_baud_t	baudRate	= SERIAL_BAUD_57600;
	int		rd	 	= 0;
	int		wr		= 0;
	char		verify		= 0;
	int		retry		= 10;
	char		*filename;

	printf("\n");
	for(i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {
				/* baud rate */
				case 'b':
					if (i == argc - 1) {
						fprintf(stderr, "Baud rate not specified\n\n");
						return 1;
					}

					baudRate = serial_get_baud(atoi(argv[++i]));
					if (baudRate == SERIAL_BAUD_INVALID) {
						fprintf(stderr,	"Invalid baud rate, valid options are:\n");
						for(baudRate = SERIAL_BAUD_1200; baudRate != SERIAL_BAUD_INVALID; ++baudRate)
							fprintf(stderr, " %d\n", serial_get_baud_int(baudRate));
						fprintf(stderr, "\n");
						return 1;
					}
					break;

				case 'r':
				case 'w':
					rd = rd || argv[i][1] == 'r';
					wr = wr || argv[i][1] == 'w';
					if (rd && wr) {
						fprintf(stderr, "Invalid options, can't read & write at the same time\n\n");
						return 1;
					}
					if (i == argc - 1) {
						fprintf(stderr, "Filename not specified\n\n");
						return 1;
					}
					filename = argv[++i];				
					break;

				case 'v':
					verify = 1;
					break;

				case 'n':
					if (i == argc - 1) {
						fprintf(stderr, "Retry not specified\n\n");
						return 1;
					}
					retry = atoi(argv[++i]);
					break;

				case 'h':
					fprintf(stderr,
						"stm32flash - http://stm32flash.googlecode.com/\n"
						"usage: %s [-brwvnh] /dev/ttyS0\n"
						"	-b rate		Baud rate (default 57600)\n"
						"	-r filename	Read flash to file\n"
						"	-w filename	Write flash to file\n"
						"	-v		Verify writes\n"
						"	-n N		Retry failed writes up to N times (default 10)\n"
						"	-h		Show this help\n"
						"\n",
						argv[0]
					);
					return 1;
					break;
			}
		} else {
			if (!device) device = argv[i];
			else {
				device = NULL;
				break;
			}
		}
	}

	if (!device || (!rd && !wr)) {
		fprintf(stderr, "Invalid usage, -h for help\n\n");
		return 1;
	}

	if (rd && verify) {
		fprintf(stderr, "Invalid usage, -v is only valid when writing\n\n");
		return 1;
	}

	if (wr) {
		wr = open(filename, O_RDONLY);
		if (wr < 0) {
			perror(filename);
			return 1;
		}
	}

	serial = serial_open(device);
	if (!serial) {
		perror(device);
		return 1;
	}

	if (serial_setup(
		serial,
		baudRate,
		SERIAL_BITS_8,
		SERIAL_PARITY_EVEN,
		SERIAL_STOPBIT_1
	) != SERIAL_ERR_OK) {
		perror(device);
		return 1;
	}

	if (!init_stm32()) goto close;

	printf("stm32flash - http://stm32flash.googlecode.com/\n");
	printf("Serial Config: %s\n", serial_get_setup_str(serial));
	printf("Version      : 0x%02x\n", stm.bl_version);
	printf("Option 1     : 0x%02x\n", stm.option1);
	printf("Option 2     : 0x%02x\n", stm.option2);
	printf("Device ID    : 0x%04x (%s)\n", stm.pid, stm.dev->name);
	printf("RAM       : %dKiB  (%db reserved by bootloader)\n", (stm.dev->ram_end - 0x20000000) / 1024, stm.dev->ram_start - 0x20000000);
	printf("Flash     : %dKiB (sector size: %dx%d)\n", (stm.dev->fl_end - stm.dev->fl_start ) / 1024, stm.dev->fl_pps, stm.dev->fl_ps);
	printf("Option RAM: %db\n", stm.dev->opt_end - stm.dev->opt_start);
	printf("System RAM: %dKiB\n", (stm.dev->mem_end - stm.dev->mem_start) / 1024);
	printf("\n");


	uint8_t		buffer[256];
	uint32_t	addr;
	unsigned int	len;
	int		failed = 0;

	if (rd) {
		rd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
		if (rd < 0) {
			perror(filename);
			goto close;
		}

		addr = stm.dev->fl_start;
		fprintf(stdout, "\x1B[s");
		fflush(stdout);
		while(addr < stm.dev->fl_end) {
			uint32_t left	= stm.dev->fl_end - addr;
			len		= sizeof(buffer) > left ? left : sizeof(buffer);
			if (!read_memory(addr, buffer, len)) {
				fprintf(stderr, "Failed to read memory at address 0x%08x\n", addr);
				goto close;
			}
			write(rd, buffer, len);
			addr += len;

			fprintf(stdout,
				"\x1B[uRead address 0x%08x (%.2f%%) ",
				addr,
				(100.0f / (float)(stm.dev->fl_end - stm.dev->fl_start)) * (float)(addr - stm.dev->fl_start)
			);
			fflush(stdout);
		}
		fprintf(stdout,	"Done.\n");
		ret = 0;
		goto close;
	}

	if (wr) {
		struct	stat st;
		off_t 	offset = 0;
		ssize_t r;
		assert(stat(filename, &st) == 0);
		if (st.st_size > stm.dev->fl_end - stm.dev->fl_start) {
			fprintf(stderr, "File provided larger then available flash space.\n");
			goto close;
		}

		addr = stm.dev->fl_start;
		fprintf(stdout, "\x1B[s");
		fflush(stdout);
		while(addr < stm.dev->fl_end && offset < st.st_size) {
			uint32_t left	= stm.dev->fl_end - addr;
			len		= sizeof(buffer) > left ? left : sizeof(buffer);
			len		= len > st.st_size - offset ? st.st_size - offset : len;
			r		= read(wr, buffer, len);
			if (r < len) {
				perror(filename);
				goto close;
			}
			
			again:
			if (!write_memory(addr, buffer, len)) {
				fprintf(stderr, "Failed to write memory at address 0x%08x\n", addr);
				goto close;
			}

			if (verify) {
				uint8_t compare[len];
				if (!read_memory(addr, compare, len)) {
					fprintf(stderr, "Failed to read memory at address 0x%08x\n", addr);
					goto close;
				}

				for(r = 0; r < len; ++r)
					if (buffer[r] != compare[r]) {
						if (failed == retry) {
							fprintf(stderr, "Failed to verify at address 0x%08x, expected 0x%02x and found 0x%02x\n",
								(uint32_t)(addr + r),
								buffer [r],
								compare[r]
							);
							goto close;
						}
						++failed;
						goto again;
					}

				failed = 0;
			}

			addr	+= len;
			offset	+= len;

			fprintf(stdout,
				"\x1B[uWrote %saddress 0x%08x (%.2f%%) ",
				verify ? "and verified " : "",
				addr,
				(100.0f / st.st_size) * offset
			);
			fflush(stdout);

		}

		fprintf(stdout,	"Done.\n");
		ret = 0;
		goto close;
	}

close:
	free_stm32();
	serial_close(serial);

	if (rd) close(rd);
	if (wr) close(wr);
	printf("\n");
	return ret;
}

inline uint32_t swap_u32(const uint32_t v) {
	if (le)
		return	((v & 0xFF000000) >> 24) |
			((v & 0x00FF0000) >>  8) |
			((v & 0x0000FF00) <<  8) |
			((v & 0x000000FF) << 24);
	return v;
}

void send_byte(const uint8_t byte) {
	serial_err_t err;
	err = serial_write(serial, &byte, 1);
	if (err != SERIAL_ERR_OK) {
		perror("send_byte");
		assert(0);
	}
}

char read_byte() {
	uint8_t byte;
	serial_err_t err;
	err = serial_read(serial, &byte, 1);
	if (err != SERIAL_ERR_OK) {
		perror("read_byte");
		assert(0);
	}
	return byte;
}

char send_command(uint8_t cmd) {
	send_byte(cmd);
	send_byte(cmd ^ 0xFF);
	if (read_byte() != STM32_ACK) {
		fprintf(stderr, "Error sending command 0x%02x to device\n", cmd);
		return 0;
	}
	return 1;
}

char init_stm32() {
	uint8_t len;
	memset(&stm, 0, sizeof(stm32_t));

	send_byte(STM32_CMD_INIT);
	if (read_byte() != STM32_ACK) {
		fprintf(stderr, "Failed to get init ACK from device\n");
		return 0;
	}

	/* get the bootloader information */
	if (!send_command(STM32_CMD_GET)) return 0;
	len            = read_byte() + 1;
	stm.bl_version = read_byte(); --len;
	stm.cmd.get    = read_byte(); --len;
	stm.cmd.gvr    = read_byte(); --len;
	stm.cmd.gid    = read_byte(); --len;
	stm.cmd.rm     = read_byte(); --len;
	stm.cmd.go     = read_byte(); --len;
	stm.cmd.wm     = read_byte(); --len;
	stm.cmd.er     = read_byte(); --len;
	stm.cmd.wp     = read_byte(); --len;
	stm.cmd.uw     = read_byte(); --len;
	stm.cmd.rp     = read_byte(); --len;
	stm.cmd.ur     = read_byte(); --len;
	if (len > 0) {
		fprintf(stderr, "Seems this bootloader returns more then we understand in the GET command, we will skip the unknown bytes\n");
		while(len-- > 0) read_byte();
	}
	if (read_byte() != STM32_ACK) return 0;
	
	/* get the version and read protection status  */
	if (!send_command(stm.cmd.gvr)) return 0;
	stm.version = read_byte();
	stm.option1 = read_byte();
	stm.option2 = read_byte();
	if (read_byte() != STM32_ACK) return 0;

	/* get the device ID */
	if (!send_command(stm.cmd.gid)) return 0;
	len     = read_byte() + 1;
	if (len != 2) {
		fprintf(stderr, "More then two bytes sent in the PID, unknown/unsupported device\n");
		return 0;
	}
	stm.pid = (read_byte() << 8) | read_byte();
	if (read_byte() != STM32_ACK) return 0;

	stm.dev = devices;
	while(stm.dev->id != 0x00 && stm.dev->id != stm.pid)
		++stm.dev;

	if (stm.dev->id == 0x00) {
		fprintf(stderr, "Unknown Device ID 0x%02x\n", stm.pid);
		return 0;
	}

	return 1;
}

void free_stm32() {

}

char erase_memory() {
	if (!send_command(stm.cmd.er)) return 0;
	if (!send_command(0xFF      )) return 0;
	return 1;
}

char read_memory(uint32_t address, uint8_t data[], unsigned int len) {
	uint8_t cs;
	unsigned int i;
	assert(len > 0 && len < 257);

	/* must be 32bit aligned */
	assert(address % 4 == 0);

	address = swap_u32(address);
	cs = ((address & 0xFF000000) >> 24) ^
	     ((address & 0x00FF0000) >> 16) ^
	     ((address & 0x0000FF00) >>  8) ^
	     ((address & 0x000000FF) >>  0);

	if (!send_command(stm.cmd.rm)) return 0;
	assert(serial_write(serial, &address, 4) == SERIAL_ERR_OK);
	send_byte(cs);
	if (read_byte() != STM32_ACK) return 0;

	i = len - 1;
	send_byte(i);
	send_byte(i ^ 0xFF);
	if (read_byte() != STM32_ACK) return 0;

	assert(serial_read(serial, data, len) == SERIAL_ERR_OK);
	return 1;
}

char write_memory(uint32_t address, uint8_t data[], unsigned int len) {
	uint8_t cs;
	unsigned int i;
	int c, extra;
	assert(len > 0 && len < 257);

	/* must be 32bit aligned */
	assert(address % 4 == 0);

	address = swap_u32(address);
	cs = ((address & 0xFF000000) >> 24) ^
	     ((address & 0x00FF0000) >> 16) ^
	     ((address & 0x0000FF00) >>  8) ^
	     ((address & 0x000000FF) >>  0);

	/* send the address and checksum */
	if (!send_command(stm.cmd.wm)) return 0;
	assert(serial_write(serial, &address, 4) == SERIAL_ERR_OK);
	send_byte(cs);
	if (read_byte() != STM32_ACK) return 0;

	/* setup the cs and send the length */
	extra = len % 4;
	cs = len - 1 + extra;
	send_byte(cs);

	/* write the data and build the checksum */
	for(i = 0; i < len; ++i)
		cs ^= data[i];

	assert(serial_write(serial, data, len) == SERIAL_ERR_OK);

	/* write the alignment padding */
	for(c = 0; c < extra; ++c) {
		send_byte(0xFF);
		cs ^= 0xFF;
	}

	/* send the checksum */
	send_byte(cs);
	return read_byte() == STM32_ACK;
}
