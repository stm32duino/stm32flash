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

#include "serial.h"

serial_baud_t serial_get_baud(unsigned int baud) {
	switch(baud) {
		case   1200: return SERIAL_BAUD_1200  ;
		case   1800: return SERIAL_BAUD_1800  ;
		case   2400: return SERIAL_BAUD_2400  ;
		case   4800: return SERIAL_BAUD_4800  ;
		case   9600: return SERIAL_BAUD_9600  ;
		case  19200: return SERIAL_BAUD_19200 ;
		case  38400: return SERIAL_BAUD_38400 ;
		case  57600: return SERIAL_BAUD_57600 ;
		case 115200: return SERIAL_BAUD_115200;
		default:
			return SERIAL_BAUD_INVALID;
	}
}

