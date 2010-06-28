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
	uint32_t	option_start, option_end;
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

int	fd;
stm32_t	stm;
char    le; //true if cpu is little-endian

uint32_t swap_u32(const uint32_t v);
void     send_byte(const uint8_t byte);
char     read_byte();
char*    read_str();
char     send_command(uint8_t cmd);
char     init_stm32();
void     free_stm32();
char     read_memory (uint32_t address, uint8_t data[], uint8_t len);
char     write_memory(uint32_t address, uint8_t data[], uint8_t len);

inline uint32_t swap_u32(const uint32_t v) {
	if (le)
		return	((v & 0xFF000000) >> 24) |
			((v & 0x00FF0000) >>  8) |
			((v & 0x0000FF00) <<  8) |
			((v & 0x000000FF) << 24);
	return v;
}

int main(int argc, char* argv[]) {
	struct termios	oldtio, newtio;

	/* detect CPU endian */
	const uint32_t x = 0x12345678;
	le = ((unsigned char*)&x)[0] == 0x78;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/ttyS0\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		perror(argv[0]);
		return 1;
	}
	fcntl(fd, F_SETFL, 0);

	tcgetattr(fd, &oldtio);
	tcgetattr(fd, &newtio);

	newtio.c_cflag &= ~(CBAUD | CSIZE | PARODD | CSTOPB | CSIZE | CRTSCTS);
	newtio.c_cflag |= B9600 | CS8 | CREAD | PARENB;
	newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	newtio.c_iflag |= (INPCK | ISTRIP);
	newtio.c_oflag &= ~OPOST;
	newtio.c_cc[VMIN ] = 0;
	newtio.c_cc[VTIME] = 10;

	tcflush  (fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &newtio);
	if (!init_stm32()) goto close;

	uint8_t buffer[256];
	if (!read_memory(stm.dev->ram_start, buffer, (uint8_t)sizeof(buffer)))
		fprintf(stderr, "Failed to read memory\n");
	

close:
	free_stm32();
	tcsetattr(fd, TCSANOW, &oldtio);
	close(fd);
	return 0;
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
	char ret;
	uint8_t cs;

	cs = 0xff - cmd;
	send_byte(cmd);
	send_byte(cs );
	ret = read_byte();
	if (ret != STM32_ACK) {
		fprintf(stderr, "Error sending command 0x%02x (cs: 0x%02x) to device, returned 0x%02x\n", cmd, cs, ret);
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
	if (init != STM32_ACK)
		return 0;

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

	printf("Version  : %02x\n", stm.bl_version);
	printf("Option 1 : %02x\n", stm.option1);
	printf("Option 2 : %02x\n", stm.option2);
	printf("Device ID: %04x (%s)\n", stm.pid, stm.dev->name);
	return 1;
}

void free_stm32() {

}

char read_memory(uint32_t address, uint8_t data[], uint8_t len) {
	uint8_t cs;
	uint8_t i;

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
	send_byte(0xFF - i);
	if (read_byte() != STM32_ACK) return 0;

	while(len-- > 0)
		*data++ = read_byte();

	return 1;
}

char write_memory(uint32_t address, uint8_t data[], uint8_t len) {
	uint8_t cs;
	uint8_t i;

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
	cs = len - 1 + (len % 4);
	send_byte(cs);

	/* write the data and build the checksum */
	for(i = 0; i < len; ++i) {
		send_byte(*data);
		cs ^= *data;
		++data;
	}

	/* write the alignment padding */
	for(i = len % 4; i >= 0; --i) {
		send_byte(0xFF);
		cs ^= 0xFF;
	}

	/* send the checksum */
	send_byte(cs);

	return 1;
}
