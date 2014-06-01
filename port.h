/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2014 Antonio Borneo <borneo.antonio@gmail.com>

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


#ifndef _H_PORT
#define _H_PORT

typedef enum {
	PORT_ERR_OK = 0,
	PORT_ERR_NOT_RECOGNIZED,
	PORT_ERR_UNKNOWN,
} port_err_t;

/* flags */
#define PORT_BYTE	(1 << 0)	/* byte (not frame) oriented */

/* all options and flags used to open and configure an interface */
struct port_options {
	const char *device;
	serial_baud_t baudRate;
	const char *serial_mode;
};

struct port_interface {
	const char *name;
	unsigned flags;
	port_err_t (*open)(struct port_interface *port, struct port_options *ops);
	port_err_t (*close)(struct port_interface *port);
	port_err_t (*read)(struct port_interface *port, void *buf, size_t nbyte);
	port_err_t (*write)(struct port_interface *port, void *buf, size_t nbyte);
	port_err_t (*gpio)(struct port_interface *port, serial_gpio_t n, int level);
	const char *(*get_cfg_str)(struct port_interface *port);
	void *private;
};

#endif
