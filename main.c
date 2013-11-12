/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2010 Geoffrey McRae <geoff@spacevs.com>
  Copyright (C) 2011 Steve Markgraf <steve@steve-m.de>

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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "init.h"
#include "utils.h"
#include "serial.h"
#include "stm32.h"
#include "parsers/parser.h"

#include "parsers/binary.h"
#include "parsers/hex.h"

/* device globals */
serial_t	*serial		= NULL;
stm32_t		*stm		= NULL;

void		*p_st		= NULL;
parser_t	*parser		= NULL;

/* settings */
char		*device		= NULL;
serial_baud_t	baudRate	= SERIAL_BAUD_57600;
char		*serial_mode	= "8e1";
int		rd	 	= 0;
int		wr		= 0;
int		wu		= 0;
int		rp		= 0;
int		ur		= 0;
int		eraseOnly	= 0;
int		npages		= 0;
int             spage           = 0;
char		verify		= 0;
int		retry		= 10;
char		exec_flag	= 0;
uint32_t	execute		= 0;
char		init_flag	= 1;
char		force_binary	= 0;
char		reset_flag	= 1;
char		*filename;
char		*gpio_seq	= NULL;
uint32_t	start_addr	= 0;
uint32_t	readwrite_len	= 0;

/* functions */
int  parse_options(int argc, char *argv[]);
void show_help(char *name);

int main(int argc, char* argv[]) {
	int ret = 1;
	parser_err_t perr;
	FILE *diag = stdout;

	fprintf(diag, "stm32flash - http://stm32flash.googlecode.com/\n\n");
	if (parse_options(argc, argv) != 0)
		goto close;

	if (rd && filename[0] == '-') {
		diag = stderr;
	}

	if (wr) {
		/* first try hex */
		if (!force_binary) {
			parser = &PARSER_HEX;
			p_st = parser->init();
			if (!p_st) {
				fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
				goto close;
			}
		}

		if (force_binary || (perr = parser->open(p_st, filename, 0)) != PARSER_ERR_OK) {
			if (force_binary || perr == PARSER_ERR_INVALID_FILE) {
				if (!force_binary) {
					parser->close(p_st);
					p_st = NULL;
				}

				/* now try binary */
				parser = &PARSER_BINARY;
				p_st = parser->init();
				if (!p_st) {
					fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
					goto close;
				}
				perr = parser->open(p_st, filename, 0);
			}

			/* if still have an error, fail */
			if (perr != PARSER_ERR_OK) {
				fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
				if (perr == PARSER_ERR_SYSTEM) perror(filename);
				goto close;
			}
		}

		fprintf(diag, "Using Parser : %s\n", parser->name);
	} else {
		parser = &PARSER_BINARY;
		p_st = parser->init();
		if (!p_st) {
			fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
			goto close;
		}
	}

	serial = serial_open(device);
	if (!serial) {
		fprintf(stderr, "Failed to open serial port: ");
		perror(device);
		goto close;
	}

	if (serial_setup(
		serial,
		baudRate,
		serial_get_bits(serial_mode),
		serial_get_parity(serial_mode),
		serial_get_stopbit(serial_mode)
	) != SERIAL_ERR_OK) {
		perror(device);
		goto close;
	}

	fprintf(diag, "Serial Config: %s\n", serial_get_setup_str(serial));
	if (init_flag && init_bl_entry(serial, gpio_seq) == 0) goto close;
	if (!(stm = stm32_init(serial, init_flag))) goto close;

	fprintf(diag, "Version      : 0x%02x\n", stm->bl_version);
	fprintf(diag, "Option 1     : 0x%02x\n", stm->option1);
	fprintf(diag, "Option 2     : 0x%02x\n", stm->option2);
	fprintf(diag, "Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
	fprintf(diag, "- RAM        : %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
	fprintf(diag, "- Flash      : %dKiB (sector size: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps);
	fprintf(diag, "- Option RAM : %db\n", stm->dev->opt_end - stm->dev->opt_start + 1);
	fprintf(diag, "- System RAM : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);

	uint8_t		buffer[256];
	uint32_t	addr, start, end;
	unsigned int	len;
	int		failed = 0;

	if (rd) {
		fprintf(diag, "\n");

		if ((perr = parser->open(p_st, filename, 1)) != PARSER_ERR_OK) {
			fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
			if (perr == PARSER_ERR_SYSTEM) perror(filename);
			goto close;
		}

		if (start_addr || readwrite_len) {
			start = start_addr;
			if (readwrite_len)
				end = start_addr + readwrite_len;
			else
				end = stm->dev->fl_end;
		} else {
			start = stm->dev->fl_start + (spage * stm->dev->fl_ps);
			end = stm->dev->fl_end;
		}
		addr = start;

		if (start < stm->dev->fl_start || end > stm->dev->fl_end) {
			fprintf(stderr, "Specified start & length are invalid\n");
			goto close;
		}

		fflush(diag);
		while(addr < end) {
			uint32_t left	= end - addr;
			len		= sizeof(buffer) > left ? left : sizeof(buffer);
			if (!stm32_read_memory(stm, addr, buffer, len)) {
				fprintf(stderr, "Failed to read memory at address 0x%08x, target write-protected?\n", addr);
				goto close;
			}
			if (parser->write(p_st, buffer, len) != PARSER_ERR_OK)
			{
				fprintf(stderr, "Failed to write data to file\n");
				goto close;
			}
			addr += len;

			fprintf(diag,
				"\rRead address 0x%08x (%.2f%%) ",
				addr,
				(100.0f / (float)(end - start)) * (float)(addr - start)
			);
			fflush(diag);
		}
		fprintf(diag,	"Done.\n");
		ret = 0;
		goto close;
	} else if (rp) {
		fprintf(stdout, "Read-Protecting flash\n");
		/* the device automatically performs a reset after the sending the ACK */
		reset_flag = 0;
		stm32_readprot_memory(stm);
		fprintf(stdout,	"Done.\n");
	} else if (ur) {
		fprintf(stdout, "Read-UnProtecting flash\n");
		/* the device automatically performs a reset after the sending the ACK */
		reset_flag = 0;
		stm32_runprot_memory(stm);
		fprintf(stdout,	"Done.\n");
	} else if (eraseOnly) {
		ret = 0;
		fprintf(stdout, "Erasing flash\n");
		if (start_addr || readwrite_len) {
			if ((start_addr % stm->dev->fl_ps) != 0
			    || (readwrite_len % stm->dev->fl_ps) != 0) {
				fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
				ret = 1;
				goto close;
			}
			spage = (start_addr - stm->dev->fl_start) / stm->dev->fl_ps;
			if (readwrite_len)
				npages = readwrite_len / stm->dev->fl_ps;
			else
				npages = (stm->dev->fl_end - stm->dev->fl_start) / stm->dev->fl_ps;
		}

		if (!spage && !npages)
			npages = 0xff; /* mass erase */

		if (!stm32_erase_memory(stm, spage, npages)) {
			fprintf(stderr, "Failed to erase memory\n");
			ret = 1;
			goto close;
		}
		
	} else if (wu) {
		fprintf(diag, "Write-unprotecting flash\n");
		/* the device automatically performs a reset after the sending the ACK */
		reset_flag = 0;
		stm32_wunprot_memory(stm);
		fprintf(diag,	"Done.\n");

	} else if (wr) {
		fprintf(diag, "\n");

		off_t 	offset = 0;
		ssize_t r;
		unsigned int size;

		/* Assume data from stdin is whole device */
		if (filename[0] == '-')
			size = stm->dev->fl_end - stm->dev->fl_start;
		else
			size = parser->size(p_st);

		if (start_addr || readwrite_len) {
			start = start_addr;
			spage = (start_addr - stm->dev->fl_start) / stm->dev->fl_ps;
			if (readwrite_len) {
				end = start_addr + readwrite_len;
				npages = (end - stm->dev->fl_start + stm->dev->fl_ps - 1) / stm->dev->fl_ps - spage;
			} else {
				end = stm->dev->fl_end;
				if (spage)
					npages = (end - stm->dev->fl_start) / stm->dev->fl_ps - spage;
				else
					npages = 0xff; /* mass erase */
			}
		} else if (!spage && !npages) {
			start = stm->dev->fl_start;
			end = stm->dev->fl_end;
			npages = 0xff; /* mass erase */
		} else {
			start = stm->dev->fl_start + (spage * stm->dev->fl_ps);
			if (npages)
				end = start + npages * stm->dev->fl_ps;
			else
				end = stm->dev->fl_end;
		}
		addr = start;

		if (start < stm->dev->fl_start || end > stm->dev->fl_end) {
			fprintf(stderr, "Specified start & length are invalid\n");
			goto close;
		}

		// TODO: It is possible to write to non-page boundaries, by reading out flash
		//       from partial pages and combining with the input data
		// if ((start % stm->dev->fl_ps) != 0 || (end % stm->dev->fl_ps) != 0) {
		//	fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
		//	goto close;
		// } 

		// TODO: If writes are not page aligned, we should probably read out existing flash
		//       contents first, so it can be preserved and combined with new data
		if (!stm32_erase_memory(stm, spage, npages)) {
			fprintf(stderr, "Failed to erase memory\n");
			goto close;
		}

		fflush(diag);
		while(addr < end && offset < size) {
			uint32_t left	= end - addr;
			len		= sizeof(buffer) > left ? left : sizeof(buffer);
			len		= len > size - offset ? size - offset : len;

			if (parser->read(p_st, buffer, &len) != PARSER_ERR_OK)
				goto close;

			if (len == 0) {
				if (filename[0] == '-') {
					break;
				} else {
					fprintf(stderr, "Failed to read input file\n");
					goto close;
				}
			}
	
			again:
			if (!stm32_write_memory(stm, addr, buffer, len)) {
				fprintf(stderr, "Failed to write memory at address 0x%08x\n", addr);
				goto close;
			}

			if (verify) {
				uint8_t compare[len];
				if (!stm32_read_memory(stm, addr, compare, len)) {
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

			fprintf(diag,
				"\rWrote %saddress 0x%08x (%.2f%%) ",
				verify ? "and verified " : "",
				addr,
				(100.0f / size) * offset
			);
			fflush(diag);

		}

		fprintf(diag,	"Done.\n");
		ret = 0;
		goto close;
	} else
		ret = 0;

close:
	if (stm && exec_flag && ret == 0) {
		if (execute == 0)
			execute = stm->dev->fl_start;

		fprintf(diag, "\nStarting execution at address 0x%08x... ", execute);
		fflush(diag);
		if (stm32_go(stm, execute)) {
			reset_flag = 0;
			fprintf(diag, "done.\n");
		} else
			fprintf(diag, "failed.\n");
	}

	if (stm && reset_flag) {
		fprintf(diag, "\nResetting device... ");
		fflush(diag);
		if (init_bl_exit(stm, serial, gpio_seq))
			fprintf(diag, "done.\n");
		else	fprintf(diag, "failed.\n");
	}

	if (p_st  ) parser->close(p_st);
	if (stm   ) stm32_close  (stm);
	if (serial) serial_close (serial);

	fprintf(diag, "\n");
	return ret;
}

int parse_options(int argc, char *argv[]) {
	int c;
	while((c = getopt(argc, argv, "b:m:r:w:e:vn:g:jkfchuos:S:i:")) != -1) {
		switch(c) {
			case 'b':
				baudRate = serial_get_baud(strtoul(optarg, NULL, 0));
				if (baudRate == SERIAL_BAUD_INVALID) {
					fprintf(stderr,	"Invalid baud rate, valid options are:\n");
					for(baudRate = SERIAL_BAUD_1200; baudRate != SERIAL_BAUD_INVALID; ++baudRate)
						fprintf(stderr, " %d\n", serial_get_baud_int(baudRate));
					return 1;
				}
				break;

			case 'm':
				if (strlen(optarg) != 3
					|| serial_get_bits(optarg) == SERIAL_BITS_INVALID
					|| serial_get_parity(optarg) == SERIAL_PARITY_INVALID
					|| serial_get_stopbit(optarg) == SERIAL_STOPBIT_INVALID) {
					fprintf(stderr, "Invalid serial mode\n");
					return 1;
				}
				serial_mode = optarg;
				break;

			case 'r':
			case 'w':
				rd = rd || c == 'r';
				wr = wr || c == 'w';
				if (rd && wr) {
					fprintf(stderr, "ERROR: Invalid options, can't read & write at the same time\n");
					return 1;
				}
				filename = optarg;
				if (filename[0] == '-') {
					force_binary = 1;
				}
				break;
			case 'e':
				if (readwrite_len || start_addr) {
					fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
					return 1;
				}
				npages = strtoul(optarg, NULL, 0);
				if (npages > 0xFF || npages < 0) {
					fprintf(stderr, "ERROR: You need to specify a page count between 0 and 255");
					return 1;
				}
				break;
			case 'u':
				wu = 1;
				if (rd || wr) {
					fprintf(stderr, "ERROR: Invalid options, can't write unprotect and read/write at the same time\n");
					return 1;
				}
				break;

			case 'j':
				rp = 1;
				if (rd || wr) {
					fprintf(stderr, "ERROR: Invalid options, can't read protect and read/write at the same time\n");
					return 1;
				}
				break;

			case 'k':
				ur = 1;
				if (rd || wr) {
					fprintf(stderr, "ERROR: Invalid options, can't read unprotect and read/write at the same time\n");
					return 1;
				}
				break;

			case 'o':
				eraseOnly = 1;
				if (rd || wr) {
					fprintf(stderr, "ERROR: Invalid options, can't erase-only and read/write at the same time\n");
					return 1;
				}
				break;				
			
			case 'v':
				verify = 1;
				break;

			case 'n':
				retry = strtoul(optarg, NULL, 0);
				break;

			case 'g':
				exec_flag = 1;
				execute   = strtoul(optarg, NULL, 0);
				if (execute % 4 != 0) {
					fprintf(stderr, "ERROR: Execution address must be word-aligned\n");
					return 1;
				}
				break;
			case 's':
				if (readwrite_len || start_addr) {
					fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
					return 1;
				}
				spage    = strtoul(optarg, NULL, 0);
				break;
			case 'S':
				if (spage || npages) {
					fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
					return 1;
				} else {
					char *pLen;
					start_addr = strtoul(optarg, &pLen, 0);
					if (*pLen == ':') {
						pLen++;
						readwrite_len = strtoul(pLen, NULL, 0);
						if (readwrite_len == 0) {
							fprintf(stderr, "ERROR: Invalid options, can't specify zero length\n");
							return 1;
						}
					}
				}
				break;
			case 'f':
				force_binary = 1;
				break;

			case 'c':
				init_flag = 0;
				break;

			case 'h':
				show_help(argv[0]);
				return 1;

			case 'i':
				gpio_seq = optarg;
				break;
		}
	}

	for (c = optind; c < argc; ++c) {
		if (device) {
			fprintf(stderr, "ERROR: Invalid parameter specified\n");
			show_help(argv[0]);
			return 1;
		}
		device = argv[c];
	}

	if (device == NULL) {
		fprintf(stderr, "ERROR: Device not specified\n");
		show_help(argv[0]);
		return 1;
	}

	if (!wr && verify) {
		fprintf(stderr, "ERROR: Invalid usage, -v is only valid when writing\n");
		show_help(argv[0]);
		return 1;
	}

	return 0;
}

void show_help(char *name) {
	fprintf(stderr,
		"Usage: %s [-bvngfhc] [-[rw] filename] /dev/ttyS0\n"
		"	-b rate		Baud rate (default 57600)\n"
		"	-m mode		Serial port mode (default 8e1)\n"
		"	-r filename	Read flash to file (or - stdout)\n"
		"	-w filename	Write flash from file (or - stdout)\n"
		"	-u		Disable the flash write-protection\n"
		"	-j		Enable the flash read-protection\n"
		"	-k		Disable the flash read-protection\n"
		"	-o		Erase only\n"
		"	-e n		Only erase n pages before writing the flash\n"
		"	-v		Verify writes\n"
		"	-n count	Retry failed writes up to count times (default 10)\n"
		"	-g address	Start execution at specified address (0 = flash start)\n"
		"	-S address[:length]	Specify start address and optionally length for\n"
		"	                   	read/write/erase operations\n"
		"	-s start_page	Flash at specified page (0 = flash start)\n"
		"	-f		Force binary parser\n"
		"	-h		Show this help\n"
		"	-c		Resume the connection (don't send initial INIT)\n"
		"			*Baud rate must be kept the same as the first init*\n"
		"			This is useful if the reset fails\n"
		"	-i GPIO_string	GPIO sequence to enter/exit bootloader mode\n"
		"			GPIO_string=[entry_seq][:[exit_seq]]\n"
		"			sequence=[-]n[,sequence]\n"
		"\n"
		"Examples:\n"
		"	Get device information:\n"
		"		%s /dev/ttyS0\n"
		"\n"
		"	Write with verify and then start execution:\n"
		"		%s -w filename -v -g 0x0 /dev/ttyS0\n"
		"\n"
		"	Read flash to file:\n"
		"		%s -r filename /dev/ttyS0\n"
		"\n"
		"	Read 100 bytes of flash from 0x1000 to stdout:\n"
		"		%s -r - -S 0x1000:100 /dev/ttyS0\n"
		"\n"
		"	Start execution:\n"
		"		%s -g 0x0 /dev/ttyS0\n"
		"\n"
		"	GPIO sequence:\n"
		"	- entry sequence: GPIO_3=low, GPIO_2=low, GPIO_2=high\n"
		"	- exit sequence: GPIO_3=high, GPIO_2=low, GPIO_2=high\n"
		"		%s -i -3,-2,2:3,-2,2 /dev/ttyS0\n",
		name,
		name,
		name,
		name,
		name,
		name,
		name
	);
}

