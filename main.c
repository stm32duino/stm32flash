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

#define STM32_ACK	0x79
#define STM32_NACK	0x1F
#define STM32_CMD_INIT	0x7F
#define STM32_CMD_GET	0x00	/* get the version and command supported */

#if 0
These are not used as we use the values the bootloader returns instead

#define STM32_CMD_GVR	0x01	/* get version and read protection status */
#define STM32_CMD_GID	0x02	/* get the chip ID */
#define STM32_CMD_RM	0x11	/* read memory */
#define STM32_CMD_GO	0x21	/* jump to user app code */
#define STM32_CMD_WM	0x31	/* write memory */
#define STM32_CMD_ER	0x43	/* erase memory */
#define STM32_CMD_EE	0x44	/* extended erase */
#define STM32_CMD_WP	0x63	/* write protect */
#define STM32_CMD_UW	0x73	/* disable write protect */
#define STM32_CMD_RP	0x82	/* read protect */
#define STM32_CMD_UR	0x92	/* disable read protect */
#endif

typedef struct {
	uint16_t	id;
	char		*name;
	uint32_t	ram_start, ram_end;
	uint32_t	fl_start, fl_end;
	uint16_t	fl_pps; // pages per sector
	uint16_t	fl_ps;  // page size
	uint32_t	opt_start, opt_end;
	uint32_t	mem_start, mem_end;
} stm32_dev_t;

const stm32_dev_t devices[] = {
	{0x412, "Low-density"      , 0x20000200, 0x20002800, 0x08000000, 0x08008000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x410, "Medium-density"   , 0x20000200, 0x20005000, 0x08000000, 0x08020000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x414, "High-density"     , 0x20000200, 0x20010000, 0x08000000, 0x08080000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x418, "Connectivity line", 0x20000200, 0x20010000, 0x08000000, 0x08040000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFB000, 0x1FFFF800},
	{0x420, "Medium-density VL", 0x20000200, 0x20002000, 0x08000000, 0x08020000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x430, "XL-density"       , 0x20000800, 0x20018000, 0x08000000, 0x08100000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFE000, 0x1FFFF800},
	{0x000}
};

typedef struct {
	uint8_t		get;
	uint8_t		gvr;
	uint8_t		gid;
	uint8_t		rm;
	uint8_t		go;
	uint8_t		wm;
	uint8_t		er; /* this may be extended erase */
//	uint8_t		ee;
	uint8_t		wp;
	uint8_t		uw;
	uint8_t		rp;
	uint8_t		ur;
} stm32_cmd_t;

typedef struct {
	uint8_t			bl_version;
	uint8_t			version;
	uint8_t			option1, option2;
	uint16_t		pid;
	stm32_cmd_t		cmd;
	const stm32_dev_t	*dev;
} stm32_t;

int		fd;
stm32_t		stm;
char		le; //true if cpu is little-endian

uint32_t swap_u32(const uint32_t v);
void     send_byte(const uint8_t byte);
char     read_byte();
char*    read_str();
char     send_command(uint8_t cmd);
char     init_stm32();
void     free_stm32();
char     erase_memory();
char     read_memory (uint32_t address, uint8_t data[], unsigned int len);
char     write_memory(uint32_t address, uint8_t data[], unsigned int len);

int main(int argc, char* argv[]) {
	struct termios oldtio, newtio;
	int i;

	/* detect CPU endian */
	const uint32_t x = 0x12345678;
	le = ((unsigned char*)&x)[0] == 0x78;
	char *device = NULL;

	int		ret		= 1;
	speed_t		baudRate	= B57600;
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
					switch(atoi(argv[++i])) {
						case   1200: baudRate = B1200  ; break;
						case   1800: baudRate = B1800  ; break;
						case   2400: baudRate = B2400  ; break;
						case   4800: baudRate = B4800  ; break;
						case   9600: baudRate = B9600  ; break;
						case  19200: baudRate = B19200 ; break;
						case  38400: baudRate = B38400 ; break;
						case  57600: baudRate = B57600 ; break;
						case 115200: baudRate = B115200; break;
						default:
							fprintf(stderr,
								"Invalid baud rate, valid options are:\n"
								" 1200\n"
								" 1800\n"
								" 2400\n"
								" 4800\n"
								" 9600\n"
								" 19200\n"
								" 38400\n"
								" 57600 (default)\n"
								" 115200\n"
							);
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

	fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		perror(device);
		return 1;
	}
	fcntl(fd, F_SETFL, 0);

	tcgetattr(fd, &oldtio);
	tcgetattr(fd, &newtio);

	cfmakeraw(&newtio);
	newtio.c_cflag |= PARENB;
	newtio.c_cc[VMIN ] = 0;
	newtio.c_cc[VTIME] = 20;
	cfsetispeed(&newtio, baudRate);
	cfsetospeed(&newtio, baudRate);

	tcflush  (fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &newtio);
	if (!init_stm32()) goto close;

	printf("stm32flash - http://stm32flash.googlecode.com/\n");
	printf("Version   : 0x%02x\n", stm.bl_version);
	printf("Option 1  : 0x%02x\n", stm.option1);
	printf("Option 2  : 0x%02x\n", stm.option2);
	printf("Device ID : 0x%04x (%s)\n", stm.pid, stm.dev->name);
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
	tcsetattr(fd, TCSANOW, &oldtio);
	close(fd);

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
	assert(write(fd, &byte, 1) == 1);
}

char read_byte() {
	char ret;
	assert(read(fd, &ret, 1) == 1);
	return ret;
}

char *read_str() {
	uint8_t	len;
	char	*ret;
	len = read_byte();
	ret = malloc(len + 1);
	read(fd, ret, len);
	ret[len] = 0;
	return ret;
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
	uint8_t init;
	uint8_t len;
	memset(&stm, 0, sizeof(stm32_t));

	init = STM32_CMD_INIT;
	assert(write(fd, &init, 1) == 1);
	assert(read (fd, &init, 1) == 1);
	if (init != STM32_ACK) {
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
	ssize_t r;
	assert(len > 0 && len < 257);

	/* must be 32bit aligned */
	assert(address % 4 == 0);

	address = swap_u32(address);
	cs = ((address & 0xFF000000) >> 24) ^
	     ((address & 0x00FF0000) >> 16) ^
	     ((address & 0x0000FF00) >>  8) ^
	     ((address & 0x000000FF) >>  0);

	if (!send_command(stm.cmd.rm)) return 0;
	write(fd, &address, 4);
	send_byte(cs);
	if (read_byte() != STM32_ACK) return 0;

	i = len - 1;
	send_byte(i);
	send_byte(i ^ 0xFF);
	if (read_byte() != STM32_ACK) return 0;

	while(len > 0) {
		r        = read(fd, data, len);
		len	-= r;
		data	+= r;
		assert(r > 0);
	}

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
	write(fd, &address, 4);
	send_byte(cs);
	if (read_byte() != STM32_ACK) return 0;

	/* setup the cs and send the length */
	extra = len % 4;
	cs = len - 1 + extra;
	send_byte(cs);

	/* write the data and build the checksum */
	for(i = 0; i < len; ++i)
		cs ^= data[i];

	assert(write(fd, data, len) == len);

	/* write the alignment padding */
	for(c = 0; c < extra; ++c) {
		send_byte(0xFF);
		cs ^= 0xFF;
	}

	/* send the checksum */
	send_byte(cs);
	return read_byte() == STM32_ACK;
}
