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
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hex.h"

typedef struct {
	int		fd;
	char		write;
	struct stat	stat;
	int		bits;

	size_t		data_len, offset;
	uint8_t		*data;
} hex_t;

void* hex_init() {
	return calloc(sizeof(hex_t), 1);
}

parser_err_t hex_open(void *storage, const char *filename, const char write) {
	hex_t *st = storage;
	if (write) {
		st->fd = open(
			filename,
			O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR  | S_IWUSR | S_IRGRP | S_IROTH
		);
	} else {
		if (stat(filename, &st->stat) != 0)
			return PARSER_ERR_INVALID_FILE;
		st->fd = open(filename, O_RDONLY);

		/* read in the file */
		char mark;
		int i;
		uint8_t checksum;
		unsigned int c;

		while(read(st->fd, &mark, 1) != 0) {
			if (mark == '\n' || mark == '\r') continue;
			if (mark != ':')
				return PARSER_ERR_INVALID_FILE;

			char buffer[9];
			buffer[8] = 0;
			if (read(st->fd, &buffer, 8) != 8)
				return PARSER_ERR_INVALID_FILE;

			checksum = 0;
			for(i = 0; i < 8; i += 2) {
				if (sscanf(&buffer[i], "%2x", &c) != 1)
					return PARSER_ERR_INVALID_FILE;
				checksum += c;
			}
		
			unsigned int reclen, address, type;
			if (sscanf(buffer, "%2x%4x%2x", &reclen, &address, &type) != 3)
				return PARSER_ERR_INVALID_FILE;

			uint8_t *record;
			if (type == 0) {
				st->data = realloc(st->data, st->data_len + reclen);
				record = &st->data[st->data_len];
				st->data_len += reclen;
			}

			buffer[2] = 0;
			for(i = 0; i < reclen; ++i) {
				if (read(st->fd, &buffer, 2) != 2 || sscanf(buffer, "%2x", &c) != 1)
					return PARSER_ERR_INVALID_FILE;
				checksum += c;
				if (type == 0)
					record[i] = c;
			}

			if (
				read(st->fd, &buffer, 2 ) != 2 ||
				sscanf(buffer, "%2x", &c) != 1 ||
				(uint8_t)(checksum + c) != 0x00
			)	return PARSER_ERR_INVALID_FILE;		
		}
	}

	st->write = write;
	return st->fd == -1 ? PARSER_ERR_SYSTEM : PARSER_ERR_OK;
}

parser_err_t hex_close(void *storage) {
	hex_t *st = storage;

	if (st->fd) close(st->fd);
	free(st);
	return PARSER_ERR_OK;
}

unsigned int hex_size(void *storage) {
	hex_t *st = storage;
	return st->data_len;
}

parser_err_t hex_read(void *storage, void *data, unsigned int *len) {
	hex_t *st = storage;
	if (st->write) return PARSER_ERR_WRONLY;

	unsigned int left = st->data_len - st->offset;
	unsigned int get  = left > *len ? *len : left;

	memcpy(data, &st->data[st->offset], get);
	st->offset += get;

	*len = get;
	return PARSER_ERR_OK;
}

parser_err_t hex_write(void *storage, void *data, unsigned int len) {
	hex_t *st = storage;
	if (!st->write) return PARSER_ERR_RDONLY;

	ssize_t r;
	while(len > 0) {
		r = write(st->fd, data, len);
		if (r < 1) return PARSER_ERR_SYSTEM;
		st->stat.st_size += r;

		len  -= r;
		data += r;
	}

	return PARSER_ERR_OK;
}

parser_t PARSER_HEX = {
	"Intel HEX",
	hex_init,
	hex_open,
	hex_close,
	hex_size,
	hex_read,
	hex_write
};

