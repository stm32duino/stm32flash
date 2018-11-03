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


#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "compiler.h"
#include "serial.h"
#include "port.h"

#if !defined(__linux__)

static port_err_t spi_open(struct port_interface __unused *port,
         struct port_options __unused *ops)
{
  return PORT_ERR_NODEV;
}

struct port_interface port_spi = {
  .name	= "spi",
  .open	= spi_open,
};

#else

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>

struct spi_priv {
  bool initialized;
  int fd;
  uint8_t mode;
  uint8_t bits;
  uint32_t speed;
};

static port_err_t spi_open(struct port_interface *port,
         struct port_options *ops) {
  struct spi_priv *h;
  int fd, ret;

  /* 1. check device name match */
  if (strncmp(ops->device, "/dev/spidev", strlen("/dev/spidev")))
    return PORT_ERR_NODEV;

  /* 2. open it device file */
  h = calloc(sizeof(*h), 1);
  if (h == NULL) {
    fprintf(stderr, "Out of memory\n");
    return PORT_ERR_UNKNOWN;
  }
  fd = open(ops->device, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "Unable to open device \"%s\"\n", ops->device);
    free(h);
    return PORT_ERR_UNKNOWN;
  }

  /* 3. configure the SPI device */
  h->mode = SPI_MODE_0;
  h->bits = 8;
  h->speed = 500000; // 500 kHz
  h->initialized = false;

  ret = ioctl(fd, SPI_IOC_WR_MODE, &h->mode);
  if (ret < 0) {
    fprintf(stderr, "Error while setting spi mode: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }
  ret = ioctl(fd, SPI_IOC_RD_MODE, &h->mode);
  if (ret < 0) {
    fprintf(stderr, "Error while verifying spi mode: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }
  ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &h->bits);
  if (ret < 0) {
    fprintf(stderr, "Error while setting bits per word: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }
  ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &h->bits);
  if (ret < 0) {
    fprintf(stderr, "Error while verifying bits per word: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }
  ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &h->speed);
  if (ret < 0) {
    fprintf(stderr, "Error while setting bus speed: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }
  ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &h->speed);
  if (ret < 0) {
    fprintf(stderr, "Error while verifying bus speed: %d\n", errno);
    close(fd);
    free(h);
    return PORT_ERR_UNKNOWN;
  }

  h->fd = fd;
  port->private = h;

  return PORT_ERR_OK;
}

static port_err_t spi_close(struct port_interface *port) {
  struct spi_priv *h;

  h = (struct spi_priv *)port->private;
  if (h == NULL)
    return PORT_ERR_UNKNOWN;
  close(h->fd);
  free(h);
  port->private = NULL;
  return PORT_ERR_OK;
}

static port_err_t spi_read(struct port_interface *port, void *buf,
         size_t nbyte) {
  struct spi_priv *h;
  int ret;
  uint8_t dummy = 0x00;
  uint32_t retry = 0;

  h = (struct spi_priv *)port->private;
  if (h == NULL)
    return PORT_ERR_UNKNOWN;

  struct spi_ioc_transfer tr[2] = {
    {
      .rx_buf = (long int)(&dummy),
      .tx_buf = (long int)(&dummy),
		  .len = 1,
    },
    {
      .rx_buf = (long int)(buf),
		  .len = nbyte,
    }
  };

  ret = ioctl(h->fd, SPI_IOC_MESSAGE(2), &tr) ;
  if(ret < 0) {
    fprintf(stderr, "Error while reading data: %d\n", errno);
    return PORT_ERR_UNKNOWN;
  }

  // Workaround for SPI init
  if (!h->initialized) {
    while(true) {
      if (retry++ >= 500) {
          return PORT_ERR_TIMEDOUT;
      }
      if (((uint8_t*)buf)[0] != 0x79 && ((uint8_t*)buf)[0] != 0x1F) {
        struct spi_ioc_transfer tr = {
          .rx_buf = (long int)(buf),
		      .len = nbyte,
        };
        ret = ioctl(h->fd, SPI_IOC_MESSAGE(1), &tr);
        if(ret < 0) {
          fprintf(stderr, "Error while reading data: %d\n", errno);
          return PORT_ERR_UNKNOWN;
        }
      } else {
        h->initialized = true;
        break;
      }
    }
  }

  return PORT_ERR_OK;
}

static port_err_t spi_write(struct port_interface *port, void *buf,
          size_t nbyte) {
  struct spi_priv *h;
  int ret;

  h = (struct spi_priv *)port->private;
  if (h == NULL)
    return PORT_ERR_UNKNOWN;

  struct spi_ioc_transfer tr = {
    .tx_buf = (long int)(buf),
	  .len = nbyte,
  };

  ret = ioctl(h->fd, SPI_IOC_MESSAGE(1), &tr) ;
  if(ret < 0) {
    fprintf(stderr, "Error while writing data: %d\n", errno);
    return PORT_ERR_UNKNOWN;
  }
  return PORT_ERR_OK;
}

static port_err_t spi_gpio(struct port_interface __unused *port,
         serial_gpio_t __unused n, int __unused level) {
  return PORT_ERR_OK;
}

static const char *spi_get_cfg_str(struct port_interface *port) {
	struct spi_priv *h;
	static char str[100];

	h = (struct spi_priv *)port->private;
	if (h == NULL)
		return "INVALID";

	snprintf(str, sizeof(str), "speed %d kHz, spi mode %d, %d bits per word", h->speed / 1000, h->mode, h->bits);
	return str;
}

static struct varlen_cmd spi_cmd_get_reply[] = {
  {0x10, 11},
  {0x11, 11},
  { /* empty */ }
};

static port_err_t spi_flush(struct port_interface __unused *port) {
  /* SPI doesn't need to be flushed */
  return PORT_ERR_OK;
}

struct port_interface port_spi = {
  .name	= "spi",
  .flags	= PORT_SPI_INIT | PORT_CMD_SOF | PORT_RETRY,
  .open	= spi_open,
  .close	= spi_close,
  .flush  = spi_flush,
  .read	= spi_read,
  .write	= spi_write,
  .gpio	= spi_gpio,
  .cmd_get_reply	= spi_cmd_get_reply,
  .get_cfg_str	= spi_get_cfg_str,
};

#endif
