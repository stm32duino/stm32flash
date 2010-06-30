#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "binary.h"

typedef struct {
	int		fd;
	char		write;
	struct stat	stat;
} binary_t;

void* binary_init() {
	return calloc(sizeof(binary_t), 1);
}

parser_err_t binary_open(void *storage, const char *filename, const char write) {
	binary_t *st = storage;
	if (write) {
		st->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
		st->stat.st_size = 0;
	} else {
		if (stat(filename, &st->stat) != 0)
			return PARSER_ERR_INVALID_FILE;
		st->fd = open(filename, O_RDONLY);
	}

	return st->fd == -1 ? PARSER_ERR_SYSTEM : PARSER_ERR_OK;
}

parser_err_t binary_close(void *storage) {
	binary_t *st = storage;

	if (st->fd) close(st->fd);
	free(st);
	return PARSER_ERR_OK;
}

unsigned int binary_size(void *storage) {
	binary_t *st = storage;
	return st->stat.st_size;
}

parser_err_t binary_read(void *storage, void *data, unsigned int *len) {
	binary_t *st = storage;
	unsigned int left = *len;

	if (st->write) return PARSER_ERR_WRONLY;

	ssize_t r;
	while(left > 0) {
		r = read(st->fd, data, left);
		if (r < 0) return PARSER_ERR_SYSTEM;
		left -= r;
		data += r;
	}

	*len = *len - left;
	return PARSER_ERR_OK;
}

parser_err_t binary_write(void *storage, void *data, unsigned int len) {
	binary_t *st = storage;
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

parser_t PARSER_BINARY = {
	"BINARY",
	binary_init,
	binary_open,
	binary_close,
	binary_size,
	binary_read,
	binary_write
};

