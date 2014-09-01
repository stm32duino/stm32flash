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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "stm32.h"
#include "port.h"
#include "utils.h"

#define STM32_ACK	0x79
#define STM32_NACK	0x1F
#define STM32_BUSY	0x76
#define STM32_ACK_ERROR	0x00

#define STM32_CMD_INIT	0x7F
#define STM32_CMD_GET	0x00	/* get the version and command supported */
#define STM32_CMD_GVR	0x01	/* get version and read protection status */
#define STM32_CMD_GID	0x02	/* get ID */
#define STM32_CMD_RM	0x11	/* read memory */
#define STM32_CMD_GO	0x21	/* go */
#define STM32_CMD_WM	0x31	/* write memory */
#define STM32_CMD_WM_NS	0x32	/* no-stretch write memory */
#define STM32_CMD_ER	0x43	/* erase */
#define STM32_CMD_EE	0x44	/* extended erase */
#define STM32_CMD_EE_NS	0x45	/* extended erase no-stretch */
#define STM32_CMD_WP	0x63	/* write protect */
#define STM32_CMD_WP_NS	0x64	/* write protect no-stretch */
#define STM32_CMD_UW	0x73	/* write unprotect */
#define STM32_CMD_UW_NS	0x74	/* write unprotect no-stretch */
#define STM32_CMD_RP	0x82	/* readout protect */
#define STM32_CMD_RP_NS	0x83	/* readout protect no-stretch */
#define STM32_CMD_UR	0x92	/* readout unprotect */
#define STM32_CMD_UR_NS	0x93	/* readout unprotect no-stretch */
#define STM32_CMD_CRC	0xA1	/* compute CRC */
#define STM32_CMD_ERR	0xFF	/* not a valid command */

#define STM32_RESYNC_TIMEOUT	10	/* seconds */
#define STM32_MASSERASE_TIMEOUT	10	/* seconds */
#define STM32_SECTERASE_TIMEOUT	5	/* seconds */
#define STM32_BLKWRITE_TIMEOUT	1	/* seconds */

#define STM32_CMD_GET_LENGTH	17	/* bytes in the reply */

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
	uint8_t	crc;
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

extern const stm32_dev_t devices[];

static uint8_t stm32_get_ack_timeout(const stm32_t *stm, time_t timeout)
{
	struct port_interface *port = stm->port;
	uint8_t byte;
	int err;
	time_t t0, t1;

	if (!(port->flags & PORT_RETRY))
		timeout = 0;

	if (timeout)
		time(&t0);

	do {
		err = port->read(port, &byte, 1);
		if (err == PORT_ERR_TIMEDOUT && timeout) {
			time(&t1);
			if (t1 < t0 + timeout)
				continue;
		}

		if (err != PORT_ERR_OK) {
			fprintf(stderr, "Failed to read ACK byte\n");
			return STM32_ACK_ERROR;
		}

		if (byte != STM32_BUSY)
			return byte;
	} while (1);
}

static uint8_t stm32_get_ack(const stm32_t *stm)
{
	return stm32_get_ack_timeout(stm, 0);
}

char stm32_send_command_timeout(const stm32_t *stm, const uint8_t cmd,
				time_t timeout)
{
	struct port_interface *port = stm->port;
	int ret;
	uint8_t buf[2];

	buf[0] = cmd;
	buf[1] = cmd ^ 0xFF;
	ret = port->write(port, buf, 2);
	if (ret != PORT_ERR_OK) {
		fprintf(stderr, "Failed to send command\n");
		return 0;
	}
	ret = stm32_get_ack_timeout(stm, timeout);
	if (ret == STM32_ACK) {
		return 1;
	} else if (ret == STM32_NACK) {
		fprintf(stderr, "Got NACK from device on command 0x%02x\n", cmd);
	} else {
		fprintf(stderr, "Unexpected reply from device on command 0x%02x\n", cmd);
	}
	return 0;
}

char stm32_send_command(const stm32_t *stm, const uint8_t cmd)
{
	return stm32_send_command_timeout(stm, cmd, 0);
}

/* if we have lost sync, send a wrong command and expect a NACK */
static int stm32_resync(const stm32_t *stm)
{
	struct port_interface *port = stm->port;
	int err;
	uint8_t buf[2], ack;
	time_t t0, t1;

	time(&t0);
	t1 = t0;

	buf[0] = STM32_CMD_ERR;
	buf[1] = STM32_CMD_ERR ^ 0xFF;
	while (t1 < t0 + STM32_RESYNC_TIMEOUT) {
		err = port->write(port, buf, 2);
		if (err != PORT_ERR_OK) {
			usleep(500000);
			time(&t1);
			continue;
		}
		err = port->read(port, &ack, 1);
		if (err != PORT_ERR_OK) {
			time(&t1);
			continue;
		}
		if (ack == STM32_NACK)
			return 1;
		time(&t1);
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

	err = port->read(port, data, len + 2);
	if (err == PORT_ERR_OK && len == data[0])
		return 1;
	if (err != PORT_ERR_OK) {
		/* restart with only one byte */
		if (stm32_resync(stm) == 0)
			return 0;
		if (!stm32_send_command(stm, cmd))
			return 0;
		err = port->read(port, data, 1);
		if (err != PORT_ERR_OK)
			return 0;
	}

	fprintf(stderr, "Re sync (len = %d)\n", data[0]);
	if (stm32_resync(stm) == 0)
		return 0;

	len = data[0];
	if (!stm32_send_command(stm, cmd))
		return 0;
	err = port->read(port, data, len + 2);
	return err == PORT_ERR_OK;
}

/*
 * Some interface, e.g. UART, requires a specific init sequence to let STM32
 * autodetect the interface speed.
 * The sequence is only required one time after reset.
 * stm32flash has command line flag "-c" to prevent sending the init sequence
 * in case it was already sent before.
 * User can easily forget adding "-c". In this case the bootloader would
 * interpret the init sequence as part of a command message, then waiting for
 * the rest of the message blocking the interface.
 * This function sends the init sequence and, in case of timeout, recovers
 * the interface.
 */
static char stm32_send_init_seq(const stm32_t *stm)
{
	struct port_interface *port = stm->port;
	int ret;
	uint8_t byte, cmd = STM32_CMD_INIT;

	ret = port->write(port, &cmd, 1);
	if (ret != PORT_ERR_OK) {
		fprintf(stderr, "Failed to send init to device\n");
		return 0;
	}
	ret = port->read(port, &byte, 1);
	if (ret == PORT_ERR_OK && byte == STM32_ACK)
		return 1;
	if (ret == PORT_ERR_OK && byte == STM32_NACK) {
		/* We could get error later, but let's continue, for now. */
		fprintf(stderr,
			"Warning: the interface was not closed properly.\n");
		return 1;
	}
	if (ret != PORT_ERR_TIMEDOUT) {
		fprintf(stderr, "Failed to init device.\n");
		return 0;
	}

	/*
	 * Check if previous STM32_CMD_INIT was taken as first byte
	 * of a command. Send a new byte, we should get back a NACK.
	 */
	ret = port->write(port, &cmd, 1);
	if (ret != PORT_ERR_OK) {
		fprintf(stderr, "Failed to send init to device\n");
		return 0;
	}
	ret = port->read(port, &byte, 1);
	if (ret == PORT_ERR_OK && byte == STM32_NACK)
		return 1;
	fprintf(stderr, "Failed to init device.\n");
	return 0;
}

/* find newer command by higher code */
#define newer(prev, a) (((prev) == STM32_CMD_ERR) \
			? (a) \
			: (((prev) > (a)) ? (prev) : (a)))

stm32_t *stm32_init(struct port_interface *port, const char init)
{
	uint8_t len, val, buf[257];
	stm32_t *stm;
	int i, new_cmds;

	stm      = calloc(sizeof(stm32_t), 1);
	stm->cmd = malloc(sizeof(stm32_cmd_t));
	memset(stm->cmd, STM32_CMD_ERR, sizeof(stm32_cmd_t));
	stm->port = port;

	if ((port->flags & PORT_CMD_INIT) && init)
		if (!stm32_send_init_seq(stm))
			return NULL;

	/* get the version and read protection status  */
	if (!stm32_send_command(stm, STM32_CMD_GVR)) {
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

	/* get the bootloader information */
	len = STM32_CMD_GET_LENGTH;
	if (port->cmd_get_reply)
		for (i = 0; port->cmd_get_reply[i].length; i++)
			if (stm->version == port->cmd_get_reply[i].version) {
				len = port->cmd_get_reply[i].length;
				break;
			}
	if (!stm32_guess_len_cmd(stm, STM32_CMD_GET, buf, len))
		return NULL;
	len = buf[0] + 1;
	stm->bl_version = buf[1];
	new_cmds = 0;
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
		case STM32_CMD_WM_NS:
			stm->cmd->wm = newer(stm->cmd->wm, val);
			break;
		case STM32_CMD_ER:
		case STM32_CMD_EE:
		case STM32_CMD_EE_NS:
			stm->cmd->er = newer(stm->cmd->er, val);
			break;
		case STM32_CMD_WP:
		case STM32_CMD_WP_NS:
			stm->cmd->wp = newer(stm->cmd->wp, val);
			break;
		case STM32_CMD_UW:
		case STM32_CMD_UW_NS:
			stm->cmd->uw = newer(stm->cmd->uw, val);
			break;
		case STM32_CMD_RP:
		case STM32_CMD_RP_NS:
			stm->cmd->rp = newer(stm->cmd->rp, val);
			break;
		case STM32_CMD_UR:
		case STM32_CMD_UR_NS:
			stm->cmd->ur = newer(stm->cmd->ur, val);
			break;
		case STM32_CMD_CRC:
			stm->cmd->crc = newer(stm->cmd->crc, val);
			break;
		default:
			if (new_cmds++ == 0)
				fprintf(stderr,
					"GET returns unknown commands (0x%2x",
					val);
			else
				fprintf(stderr, ", 0x%2x", val);
		}
	}
	if (new_cmds)
		fprintf(stderr, ")\n");
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
	if (port->write(port, buf, 5) != PORT_ERR_OK)
		return 0;
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
	int ret;
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
	if (port->write(port, buf, 5) != PORT_ERR_OK)
		return 0;
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

	ret = stm32_get_ack_timeout(stm, STM32_BLKWRITE_TIMEOUT);
	return ret == STM32_ACK;
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

char stm32_erase_memory(const stm32_t *stm, uint8_t spage, uint8_t pages)
{
	struct port_interface *port = stm->port;
	int ret;

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

	/* The erase command reported by the bootloader is either 0x43, 0x44 or 0x45 */
	/* 0x44 is Extended Erase, a 2 byte based protocol and needs to be handled differently. */
	/* 0x45 is clock no-stretching version of Extended Erase for I2C port. */
	if (stm->cmd->er != STM32_CMD_ER) {
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
			if (port->write(port, buf, 3) != PORT_ERR_OK) {
				fprintf(stderr, "Mass erase error.\n");
				return 0;
			}
			ret = stm32_get_ack_timeout(stm, STM32_MASSERASE_TIMEOUT);
			if (ret != STM32_ACK) {
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
		ret = port->write(port, buf, i);
		free(buf);
		if (ret != PORT_ERR_OK) {
			fprintf(stderr, "Page-by-page erase error.\n");
			return 0;
		}

		ret = stm32_get_ack_timeout(stm, STM32_SECTERASE_TIMEOUT);
		if (ret != STM32_ACK) {
 			fprintf(stderr, "Page-by-page erase failed. Check the maximum pages your device supports.\n");
			return 0;
 		}

 		return 1;
	}

	/* And now the regular erase (0x43) for all other chips */
	if (pages == 0xFF) {
		return stm32_send_command_timeout(stm, 0xFF, STM32_MASSERASE_TIMEOUT);
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
		ret = port->write(port, buf, i);
		free(buf);
		if (ret != PORT_ERR_OK) {
			fprintf(stderr, "Erase failed.\n");
			return 0;
		}
		ret = stm32_get_ack_timeout(stm, STM32_MASSERASE_TIMEOUT);
		return ret == STM32_ACK;
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

char stm32_go(const stm32_t *stm, uint32_t address)
{
	struct port_interface *port = stm->port;
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
	if (port->write(port, buf, 5) != PORT_ERR_OK)
		return 0;

	return stm32_get_ack(stm) == STM32_ACK;
}

char stm32_reset_device(const stm32_t *stm) {
	uint32_t target_address = stm->dev->ram_start;
	
	return stm32_run_raw_code(stm, target_address, stm_reset_code, stm_reset_code_length);
}

char stm32_crc_memory(const stm32_t *stm, uint32_t address, uint32_t length,
		      uint32_t *crc)
{
	struct port_interface *port = stm->port;
	uint8_t buf[5];

	if (stm->cmd->crc == STM32_CMD_ERR) {
		fprintf(stderr, "Error: CRC command not implemented in bootloader.\n");
		return 0;
	}

	if (!stm32_send_command(stm, stm->cmd->crc))
		return 0;

	buf[0] = address >> 24;
	buf[1] = (address >> 16) & 0xFF;
	buf[2] = (address >> 8) & 0xFF;
	buf[3] = address & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
	if (port->write(port, buf, 5) != PORT_ERR_OK)
		return 0;

	if (stm32_get_ack(stm) != STM32_ACK)
		return 0;

	buf[0] = length >> 24;
	buf[1] = (length >> 16) & 0xFF;
	buf[2] = (length >> 8) & 0xFF;
	buf[3] = length & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
	if (port->write(port, buf, 5) != PORT_ERR_OK)
		return 0;

	if (stm32_get_ack(stm) != STM32_ACK)
		return 0;

	if (stm32_get_ack(stm) != STM32_ACK)
		return 0;

	if (port->read(port, buf, 5) != PORT_ERR_OK)
		return 0;

	if (buf[4] != (buf[0] ^ buf[1] ^ buf[2] ^ buf[3]))
		return 0;

	*crc = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	return 1;
}

#define CRCPOLY_BE	0x04c11db7
#define CRC_MSBMASK	0x80000000
#define CRC_INIT_VALUE	0xFFFFFFFF
uint32_t stm32_sw_crc(uint32_t crc, uint8_t *buf, unsigned int len)
{
	int i;
	uint32_t data;

	while (len) {
		data = *buf++;
		data |= ((len > 1) ? *buf++ : 0xff) << 8;
		data |= ((len > 2) ? *buf++ : 0xff) << 16;
		data |= ((len > 3) ? *buf++ : 0xff) << 24;
		len -= 4;

		crc ^= data;

		for (i = 0; i < 32; i++)
			if (crc & CRC_MSBMASK)
				crc = (crc << 1) ^ CRCPOLY_BE;
			else
				crc = (crc << 1);
	}
	return crc;
}

char stm32_crc_wrapper(const stm32_t *stm, uint32_t address, uint32_t length,
		       uint32_t *crc)
{
	uint8_t buf[256];
	uint32_t len, current_crc;

	if (stm->cmd->crc != STM32_CMD_ERR)
		return stm32_crc_memory(stm, address, length, crc);

	current_crc = CRC_INIT_VALUE;
	while (length) {
		len = length > 256 ? 256 : length;
		if (!stm32_read_memory(stm, address, buf, len)) {
			fprintf(stderr,
				"Failed to read memory at address 0x%08x, target write-protected?\n",
				address);
			return 0;
		}
		current_crc = stm32_sw_crc(current_crc, buf, len);
		length -= len;
		address += len;
	}
	*crc = current_crc;
	return 1;
}
