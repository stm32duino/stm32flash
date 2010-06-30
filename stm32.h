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


#ifndef _STM32_H
#define _STM32_H

#include <stdint.h>
#include "serial.h"

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

typedef struct {
	uint8_t get;
	uint8_t gvr;
	uint8_t gid;
	uint8_t rm;
	uint8_t go;
	uint8_t wm;
	uint8_t er; /* this may be extended erase */
//      uint8_t ee;
	uint8_t wp;
	uint8_t uw;
	uint8_t rp;
	uint8_t ur;
} stm32_cmd_t;

typedef struct {
	const serial_t		*serial;
	uint8_t			bl_version;
	uint8_t			version;
	uint8_t			option1, option2;
	uint16_t		pid;
	stm32_cmd_t		cmd;
	const stm32_dev_t	*dev;
} stm32_t;

stm32_t* stm32_init    (const serial_t *serial);
void stm32_close       (stm32_t *stm);
char stm32_read_memory (const stm32_t *stm, uint32_t address, uint8_t data[], unsigned int len);
char stm32_write_memory(const stm32_t *stm, uint32_t address, uint8_t data[], unsigned int len);
char stm32_erase_memory(const stm32_t *stm);
char stm32_go          (const stm32_t *stm, uint32_t address);

#endif
