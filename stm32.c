/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright 2010 Geoffrey McRae <geoff@spacevs.com>
  Copyright 2012-2014 Tormod Volden <debian.tormod@gmail.com>

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

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "stm32.h"
#include "port.h"
#include "utils.h"

#define STM32_ACK	0x79
#define STM32_NACK	0x1F
#define STM32_CMD_INIT	0x7F
#define STM32_CMD_GET	0x00	/* get the version and command supported */
#define STM32_CMD_GVR	0x01	/* get version and read protection status */
#define STM32_CMD_GID	0x02	/* get ID */
#define STM32_CMD_RM	0x11	/* read memory */
#define STM32_CMD_GO	0x21	/* go */
#define STM32_CMD_WM	0x31	/* write memory */
#define STM32_CMD_ER	0x43	/* erase */
#define STM32_CMD_EE	0x44	/* extended erase */
#define STM32_CMD_WP	0x63	/* write protect */
#define STM32_CMD_UW	0x73	/* write unprotect */
#define STM32_CMD_RP	0x82	/* readout protect */
#define STM32_CMD_UR	0x92	/* readout unprotect */
#define STM32_CMD_ERR	0xFF	/* not a valid command */

#define STM32_RESYNC_TIMEOUT	10	/* seconds */

struct stm32_cmd {
	uint8_t get;
	uint8_t gvr;
	uint8_t gid;
	uint8_t rm;
	uint8_t go;
	uint8_t wm;
	uint8_t er; /* this may be extended erase */
	uint8_t wp;
	uint8_t uw;
	uint8_t rp;
	uint8_t ur;
};

/* Reset code for ARMv7-M (Cortex-M3) and ARMv6-M (Cortex-M0)
 * see ARMv7-M or ARMv6-M Architecture Reference Manual (table B3-8)
 * or "The definitive guide to the ARM Cortex-M3", section 14.4.
 */
static const uint8_t stm_reset_code[] = {
	0x01, 0x49,		// ldr     r1, [pc, #4] ; (<AIRCR_OFFSET>)
	0x02, 0x4A,		// ldr     r2, [pc, #8] ; (<AIRCR_RESET_VALUE>)
	0x0A, 0x60,		// str     r2, [r1, #0]
	0xfe, 0xe7,		// endless: b endless
	0x0c, 0xed, 0x00, 0xe0,	// .word 0xe000ed0c <AIRCR_OFFSET> = NVIC AIRCR register address
	0x04, 0x00, 0xfa, 0x05	// .word 0x05fa0004 <AIRCR_RESET_VALUE> = VECTKEY | SYSRESETREQ
};

static const uint32_t stm_reset_code_length = sizeof(stm_reset_code);

/* Device table, corresponds to the "Bootloader device-dependent parameters"
 * table in ST document AN2606.
 * Note that the option bytes upper range is inclusive!
 */
const stm32_dev_t devices[] = {
	/* F0 */
	{0x440, "STM32F051xx"       , 0x20001000, 0x20002000, 0x08000000, 0x08010000,  4, 1024, 0x1FFFF800, 0x1FFFF80B, 0x1FFFEC00, 0x1FFFF800},
	{0x444, "STM32F030/F031"    , 0x20001000, 0x20002000, 0x08000000, 0x08010000,  4, 1024, 0x1FFFF800, 0x1FFFF80B, 0x1FFFEC00, 0x1FFFF800},
	{0x448, "STM32F072xx"       , 0x20001800, 0x20004000, 0x08000000, 0x08010000,  4, 1024, 0x1FFFF800, 0x1FFFF80B, 0x1FFFEC00, 0x1FFFF800},
	/* F1 */
	{0x412, "Low-density"       , 0x20000200, 0x20002800, 0x08000000, 0x08008000,  4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x410, "Medium-density"    , 0x20000200, 0x20005000, 0x08000000, 0x08020000,  4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x414, "High-density"      , 0x20000200, 0x20010000, 0x08000000, 0x08080000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x420, "Medium-density VL" , 0x20000200, 0x20002000, 0x08000000, 0x08020000,  4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x428, "High-density VL"   , 0x20000200, 0x20008000, 0x08000000, 0x08080000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x418, "Connectivity line" , 0x20001000, 0x20010000, 0x08000000, 0x08040000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFB000, 0x1FFFF800},
	{0x430, "XL-density"        , 0x20000800, 0x20018000, 0x08000000, 0x08100000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFE000, 0x1FFFF800},
	/* Note that F2 and F4 devices have sectors of different page sizes
           and only the first sectors (of one page size) are included here */
	/* F2 */
	{0x411, "STM32F2xx"         , 0x20002000, 0x20020000, 0x08000000, 0x08100000,  4, 16384, 0x1FFFC000, 0x1FFFC00F, 0x1FFF0000, 0x1FFF77DF},
	/* F3 */
	{0x432, "STM32F373/8"       , 0x20001400, 0x20008000, 0x08000000, 0x08040000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFD800, 0x1FFFF800},
	{0x422, "F302xB/303xB/358"  , 0x20001400, 0x20010000, 0x08000000, 0x08040000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFD800, 0x1FFFF800},
	{0x439, "STM32F302"         , 0x20001800, 0x20004000, 0x08000000, 0x08040000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFD800, 0x1FFFF800},
	{0x438, "F303x4/334/328"    , 0x20001800, 0x20003000, 0x08000000, 0x08040000,  2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFD800, 0x1FFFF800},
	/* F4 */
	{0x413, "STM32F40/1"        , 0x20002000, 0x20020000, 0x08000000, 0x08100000,  4, 16384, 0x1FFFC000, 0x1FFFC00F, 0x1FFF0000, 0x1FFF77DF},
	/* 0x419 is also used for STM32F429/39 but with other bootloader ID... */
	{0x419, "STM32F427/37"      , 0x20002000, 0x20030000, 0x08000000, 0x08100000,  4, 16384, 0x1FFFC000, 0x1FFFC00F, 0x1FFF0000, 0x1FFF77FF},
	{0x423, "STM32F401xB(C)"    , 0x20003000, 0x20010000, 0x08000000, 0x08100000,  4, 16384, 0x1FFFC000, 0x1FFFC00F, 0x1FFF0000, 0x1FFF77FF},
	{0x433, "STM32F401xD(E)"    , 0x20003000, 0x20018000, 0x08000000, 0x08100000,  4, 16384, 0x1FFFC000, 0x1FFFC00F, 0x1FFF0000, 0x1FFF77FF},
	/* L0 */
	{0x417, "L05xxx/06xxx"      , 0x20001000, 0x20002000, 0x08000000, 0x08020000, 16,  256, 0x1FF80000, 0x1FF8000F, 0x1FF00000, 0x1FF01000},
	/* L1 */
	{0x416, "L1xxx6(8/B)"       , 0x20000800, 0x20004000, 0x08000000, 0x08020000, 16,  256, 0x1FF80000, 0x1FF8000F, 0x1FF00000, 0x1FF01000},
	{0x429, "L1xxx6(8/B)A"      , 0x20001000, 0x20008000, 0x08000000, 0x08020000, 16,  256, 0x1FF80000, 0x1FF8000F, 0x1FF00000, 0x1FF01000},
	{0x427, "L1xxxC"            , 0x20001000, 0x20008000, 0x08000000, 0x08020000, 16,  256, 0x1FF80000, 0x1FF8000F, 0x1FF00000, 0x1FF02000},
	{0x436, "L1xxxD"            , 0x20001000, 0x2000C000, 0x08000000, 0x08060000, 16,  256, 0x1ff80000, 0x1ff8000F, 0x1FF00000, 0x1FF02000},
	{0x437, "L1xxxE"            , 0x20001000, 0x20014000, 0x08000000, 0x08060000, 16,  256, 0x1ff80000, 0x1ff8000F, 0x1FF00000, 0x1FF02000},
	/* These are not (yet) in AN2606: */
	{0x641, "Medium_Density PL" , 0x20000200, 0x00005000, 0x08000000, 0x08020000,  4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x9a8, "STM32W-128K"       , 0x20000200, 0x20002000, 0x08000000, 0x08020000,  1, 1024, 0, 0, 0, 0},
	{0x9b0, "STM32W-256K"       , 0x20000200, 0x20004000, 0x08000000, 0x08040000,  1, 2048, 0, 0, 0, 0},
	{0x0}
};

/* internal functions */
char    stm32_send_command(const stm32_t *stm, const uint8_t cmd);


static void stm32_send(const stm32_t *stm, uint8_t *byte, unsigned int len)
{
	struct port_interface *port = stm->port;
	int err;

	err = port->write(port, byte, len);
	if (err != PORT_ERR_OK) {
		fprintf(stderr, "Failed to send: ");
		perror("send_byte");
		exit(1);
	}
}

static uint8_t stm32_get_ack(const stm32_t *stm)
{
	struct port_interface *port = stm->port;
	uint8_t byte;
	int err;

	err = port->read(port, &byte, 1);
	if (err != PORT_ERR_OK) {
		fprintf(stderr, "Failed to read byte: ");
		perror("read_byte");
		exit(1);
	}
	return byte;
}

char stm32_send_command(const stm32_t *stm, const uint8_t cmd) {
	int ret;
	uint8_t buf[2];

	buf[0] = cmd;
	buf[1] = cmd ^ 0xFF;
	stm32_send(stm, buf, 2);
	ret = stm32_get_ack(stm);
	if (ret == STM32_ACK) {
		return 1;
	} else if (ret == STM32_NACK) {
		fprintf(stderr, "Got NACK from device on command 0x%02x\n", cmd);
	} else {
		fprintf(stderr, "Unexpected reply from device on command 0x%02x\n", cmd);
	}
	return 0;
}

/* if we have lost sync, send a wrong command and expect a NACK */
static int stm32_resync(const stm32_t *stm)
{
	struct port_interface *port = stm->port;
	int err;
	uint8_t buf[2], ack;
	int i = 0;

	buf[0] = STM32_CMD_ERR;
	buf[1] = STM32_CMD_ERR ^ 0xFF;
	while (i++ < STM32_RESYNC_TIMEOUT) {
		err = port->write(port, buf, 2);
		if (err != PORT_ERR_OK) {
			sleep(1);
			continue;
		}
		err = port->read(port, &ack, 1);
		if (err != PORT_ERR_OK) {
			sleep(1);
			continue;
		}
		if (ack == STM32_NACK)
			return 1;
		sleep(1);
	}
	return 0;
}

/*
 * some command receive reply frame with variable lenght, and lenght is
 * embedded in reply frame itself.
 * We can guess the lenght, but if we guess wrong the protocol gets out
 * of sync.
 * Use resync for frame oriented interfaces (e.g. I2C) and byte-by-byte
 * read for byte oriented interfaces (e.g. UART).
 *
 * to run safely, data buffer should be allocated for 256+1 bytes
 *
 * len is value of the first byte in the frame.
 */
static int stm32_guess_len_cmd(const stm32_t *stm, uint8_t cmd,
				uint8_t *data, unsigned int len) {
	struct port_interface *port = stm->port;
	int err;

	if (!stm32_send_command(stm, cmd))
		return 0;
	if (port->flags & PORT_BYTE) {
		/* interface is UART-like */
		err = port->read(port, data, 1);
		if (err != PORT_ERR_OK)
			return 0;
		len = data[0];
		err = port->read(port, data + 1, len + 1);
		return err == PORT_ERR_OK;
	}
	data[0] = 0;
	err = port->read(port, data, len + 2);
	if (data[0] == 0 && err != PORT_ERR_OK)
		return 0;
	if (len == data[0])
		return 1;

	fprintf(stderr, "Re sync (len = %d)\n", data[0]);
	if (stm32_resync(stm) == 0)
		return 0;

	len = data[0];
	if (!stm32_send_command(stm, cmd))
		return 0;
	err = port->read(port, data, len + 2);
	return err == PORT_ERR_OK;
}

stm32_t *stm32_init(struct port_interface *port, const char init)
{
	uint8_t len, val, buf[257];
	stm32_t *stm;
	int i;

	stm      = calloc(sizeof(stm32_t), 1);
	stm->cmd = malloc(sizeof(stm32_cmd_t));
	memset(stm->cmd, STM32_CMD_ERR, sizeof(stm32_cmd_t));
	stm->port = port;

	if ((port->flags & PORT_CMD_INIT) && init) {
		buf[0] = STM32_CMD_INIT;
		stm32_send(stm, buf, 1);
		if (stm32_get_ack(stm) != STM32_ACK) {
			stm32_close(stm);
			fprintf(stderr, "Failed to get init ACK from device\n");
			return NULL;
		}
	}

	/* get the bootloader information */
	if (!stm32_guess_len_cmd(stm, STM32_CMD_GET, buf, 10))
		return NULL;
	len = buf[0] + 1;
	stm->bl_version = buf[1];
	for (i = 1; i < len; i++) {
		val = buf[i + 1];
		switch (val) {
		case STM32_CMD_GET:
			stm->cmd->get = val; break;
		case STM32_CMD_GVR:
			stm->cmd->gvr = val; break;
		case STM32_CMD_GID:
			stm->cmd->gid = val; break;
		case STM32_CMD_RM:
			stm->cmd->rm = val; break;
		case STM32_CMD_GO:
			stm->cmd->go = val; break;
		case STM32_CMD_WM:
			stm->cmd->wm = val; break;
		case STM32_CMD_ER:
		case STM32_CMD_EE:
			stm->cmd->er = val; break;
		case STM32_CMD_WP:
			stm->cmd->wp = val; break;
		case STM32_CMD_UW:
			stm->cmd->uw = val; break;
		case STM32_CMD_RP:
			stm->cmd->rp = val; break;
		case STM32_CMD_UR:
			stm->cmd->ur = val; break;
		default:
			fprintf(stderr,
				"Seems this bootloader returns more then we "
				"understand in the GET command.\n"
				"We will skip unknown byte 0x%02x\n", val);
		}
	}
	if (stm32_get_ack(stm) != STM32_ACK) {
		stm32_close(stm);
		return NULL;
	}

	if (stm->cmd->get == STM32_CMD_ERR
	    || stm->cmd->gvr == STM32_CMD_ERR
	    || stm->cmd->gid == STM32_CMD_ERR) {
		fprintf(stderr, "Error: bootloader did not returned correct "
			"information from GET command\n");
		return NULL;
	}

	/* get the version and read protection status  */
	if (!stm32_send_command(stm, stm->cmd->gvr)) {
		stm32_close(stm);
		return NULL;
	}

	/* From AN, only UART bootloader returns 3 bytes */
	len = (port->flags & PORT_GVR_ETX) ? 3 : 1;
	if (port->read(port, buf, len) != PORT_ERR_OK)
		return NULL;
	stm->version = buf[0];
	stm->option1 = (port->flags & PORT_GVR_ETX) ? buf[1] : 0;
	stm->option2 = (port->flags & PORT_GVR_ETX) ? buf[2] : 0;
	if (stm32_get_ack(stm) != STM32_ACK) {
		stm32_close(stm);
		return NULL;
	}

	/* get the device ID */
	if (!stm32_guess_len_cmd(stm, stm->cmd->gid, buf, 1)) {
		stm32_close(stm);
		return NULL;
	}
	len = buf[0] + 1;
	if (len < 2) {
		stm32_close(stm);
		fprintf(stderr, "Only %d bytes sent in the PID, unknown/unsupported device\n", len);
		return NULL;
	}
	stm->pid = (buf[1] << 8) | buf[2];
	if (len > 2) {
		fprintf(stderr, "This bootloader returns %d extra bytes in PID:", len);
		for (i = 2; i <= len ; i++)
			fprintf(stderr, " %02x", buf[i]);
		fprintf(stderr, "\n");
	}
	if (stm32_get_ack(stm) != STM32_ACK) {
		stm32_close(stm);
		return NULL;
	}

	stm->dev = devices;
	while(stm->dev->id != 0x00 && stm->dev->id != stm->pid)
		++stm->dev;

	if (!stm->dev->id) {
		fprintf(stderr, "Unknown/unsupported device (Device ID: 0x%03x)\n", stm->pid);
		stm32_close(stm);
		return NULL;
	}

	return stm;
}

void stm32_close(stm32_t *stm) {
	if (stm) free(stm->cmd);
	free(stm);
}

char stm32_read_memory(const stm32_t *stm, uint32_t address, uint8_t data[], unsigned int len) {
	struct port_interface *port = stm->port;
	uint8_t buf[5];
	assert(len > 0 && len < 257);

	/* must be 32bit aligned */
	assert(address % 4 == 0);

	if (stm->cmd->rm == STM32_CMD_ERR) {
		fprintf(stderr, "Error: READ command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->rm)) return 0;

	buf[0] = address >> 24;
	buf[1] = (address >> 16) & 0xFF;
	buf[2] = (address >> 8) & 0xFF;
	buf[3] = address & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
	stm32_send(stm, buf, 5);
	if (stm32_get_ack(stm) != STM32_ACK)
		return 0;

	if (!stm32_send_command(stm, len - 1))
		return 0;

	if (port->read(port, data, len) != PORT_ERR_OK)
		return 0;

	return 1;
}

char stm32_write_memory(const stm32_t *stm, uint32_t address, const uint8_t data[], unsigned int len) {
	struct port_interface *port = stm->port;
	uint8_t cs, buf[256 + 2];
	unsigned int i, aligned_len;
	assert(len > 0 && len < 257);

	/* must be 32bit aligned */
	assert(address % 4 == 0);

	if (stm->cmd->wm == STM32_CMD_ERR) {
		fprintf(stderr, "Error: WRITE command not implemented in bootloader.\n");
		return 0;
	}

	/* send the address and checksum */
	if (!stm32_send_command(stm, stm->cmd->wm)) return 0;

	buf[0] = address >> 24;
	buf[1] = (address >> 16) & 0xFF;
	buf[2] = (address >> 8) & 0xFF;
	buf[3] = address & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
	stm32_send(stm, buf, 5);
	if (stm32_get_ack(stm) != STM32_ACK)
		return 0;

	aligned_len = (len + 3) & ~3;
	cs = aligned_len - 1;
	buf[0] = aligned_len - 1;
	for (i = 0; i < len; i++) {
		cs ^= data[i];
		buf[i + 1] = data[i];
	}
	/* padding data */
	for (i = len; i < aligned_len; i++) {
		cs ^= 0xFF;
		buf[i + 1] = 0xFF;
	}
	buf[aligned_len + 1] = cs;
	if (port->write(port, buf, aligned_len + 2) != PORT_ERR_OK)
		return 0;

	return stm32_get_ack(stm) == STM32_ACK;
}

char stm32_wunprot_memory(const stm32_t *stm) {
	if (stm->cmd->uw == STM32_CMD_ERR) {
		fprintf(stderr, "Error: WRITE UNPROTECT command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->uw)) return 0;
	if (!stm32_send_command(stm, 0x8C        )) return 0;
	return 1;
}

char stm32_runprot_memory  (const stm32_t *stm) {
	if (stm->cmd->ur == STM32_CMD_ERR) {
		fprintf(stderr, "Error: READ UNPROTECT command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->ur)) return 0;
	if (!stm32_send_command(stm, 0x6D        )) return 0;
	return 1;
}

char stm32_readprot_memory(const stm32_t *stm) {
	if (stm->cmd->rp == STM32_CMD_ERR) {
		fprintf(stderr, "Error: READ PROTECT command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->rp)) return 0;
	if (!stm32_send_command(stm, 0x7D        )) return 0;
	return 1;
}

char stm32_erase_memory(const stm32_t *stm, uint8_t spage, uint8_t pages) {

	if (!pages)
		return 1;

	if (stm->cmd->er == STM32_CMD_ERR) {
		fprintf(stderr, "Error: ERASE command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->er)) {
		fprintf(stderr, "Can't initiate chip erase!\n");
		return 0;
	}

	/* The erase command reported by the bootloader is either 0x43 or 0x44 */
	/* 0x44 is Extended Erase, a 2 byte based protocol and needs to be handled differently. */
	if (stm->cmd->er == STM32_CMD_EE) {
 		/* Not all chips using Extended Erase support mass erase */
 		/* Currently known as not supporting mass erase is the Ultra Low Power STM32L15xx range */
 		/* So if someone has not overridden the default, but uses one of these chips, take it out of */
 		/* mass erase mode, so it will be done page by page. This maximum might not be correct either! */
		if (stm->pid == 0x416 && pages == 0xFF) pages = 0xF8; /* works for the STM32L152RB with 128Kb flash */

		if (pages == 0xFF) {
			uint8_t buf[3];

			/* 0xFFFF the magic number for mass erase */
			buf[0] = 0xFF;
			buf[1] = 0xFF;
			buf[2] = 0x00;	/* checksum */
			stm32_send(stm, buf, 3);
			if (stm32_get_ack(stm) != STM32_ACK) {
				fprintf(stderr, "Mass erase failed. Try specifying the number of pages to be erased.\n");
				return 0;
			}
			return 1;
		}

		uint16_t pg_num;
		uint8_t pg_byte;
 		uint8_t cs = 0;
		uint8_t *buf;
		int i = 0;

		buf = malloc(2 + 2 * pages + 1);
		if (!buf)
			return 0;
 
		/* Number of pages to be erased - 1, two bytes, MSB first */
		pg_byte = (pages - 1) >> 8;
		buf[i++] = pg_byte;
		cs ^= pg_byte;
		pg_byte = (pages - 1) & 0xFF;
		buf[i++] = pg_byte;
		cs ^= pg_byte;
 
		for (pg_num = spage; pg_num < spage + pages; pg_num++) {
 			pg_byte = pg_num >> 8;
 			cs ^= pg_byte;
			buf[i++] = pg_byte;
 			pg_byte = pg_num & 0xFF;
 			cs ^= pg_byte;
			buf[i++] = pg_byte;
 		}
		buf[i++] = cs;
		stm32_send(stm, buf, i);
		free(buf);
 	
		if (stm32_get_ack(stm) != STM32_ACK) {
 			fprintf(stderr, "Page-by-page erase failed. Check the maximum pages your device supports.\n");
			return 0;
 		}

 		return 1;
	}

	/* And now the regular erase (0x43) for all other chips */
	if (pages == 0xFF) {
		return stm32_send_command(stm, 0xFF);
	} else {
		uint8_t cs = 0;
		uint8_t pg_num;
		uint8_t *buf;
		int i = 0;

		buf = malloc(1 + pages + 1);
		if (!buf)
			return 0;

		buf[i++] = pages - 1;
		cs ^= (pages-1);
		for (pg_num = spage; pg_num < (pages + spage); pg_num++) {
			buf[i++] = pg_num;
			cs ^= pg_num;
		}
		buf[i++] = cs;
		stm32_send(stm, buf, i);
		free(buf);
		return stm32_get_ack(stm) == STM32_ACK;
	}
}

char stm32_run_raw_code(const stm32_t *stm, uint32_t target_address, const uint8_t *code, uint32_t code_size)
{
	uint32_t stack_le = le_u32(0x20002000);
	uint32_t code_address_le = le_u32(target_address + 8);
	uint32_t length = code_size + 8;
	
	/* Must be 32-bit aligned */
	assert(target_address % 4 == 0);

	uint8_t *mem = malloc(length);
	if (!mem)
		return 0;
	
	memcpy(mem, &stack_le, sizeof(uint32_t));
	memcpy(mem + 4, &code_address_le, sizeof(uint32_t));
	memcpy(mem + 8, code, code_size);
	
	uint8_t *pos = mem;
	uint32_t address = target_address;
	while(length > 0) {
		
		uint32_t w = length > 256 ? 256 : length;
		if (!stm32_write_memory(stm, address, pos, w)) {
			free(mem);
			return 0;
		}
		
		address += w;
		pos += w;
		length -=w;
	}
	
	free(mem);
	return stm32_go(stm, target_address);
}

char stm32_go(const stm32_t *stm, uint32_t address) {
	uint8_t buf[5];

        if (stm->cmd->go == STM32_CMD_ERR) {
                fprintf(stderr, "Error: GO command not implemented in bootloader.\n");
                return 0;
        }

	if (!stm32_send_command(stm, stm->cmd->go)) return 0;

	buf[0] = address >> 24;
	buf[1] = (address >> 16) & 0xFF;
	buf[2] = (address >> 8) & 0xFF;
	buf[3] = address & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
	stm32_send(stm, buf, 5);

	return stm32_get_ack(stm) == STM32_ACK;
}

char stm32_reset_device(const stm32_t *stm) {
	uint32_t target_address = stm->dev->ram_start;
	
	return stm32_run_raw_code(stm, target_address, stm_reset_code, stm_reset_code_length);
}

