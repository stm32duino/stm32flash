#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdint.h>

typedef struct serial serial_t;

typedef enum {
	SERIAL_PARITY_NONE,
	SERIAL_PARITY_EVEN,
	SERIAL_PARITY_ODD
} serial_parity_t;

typedef enum {
	SERIAL_BITS_5,
	SERIAL_BITS_6,
	SERIAL_BITS_7,
	SERIAL_BITS_8
} serial_bits_t;

typedef enum {
	SERIAL_BAUD_1200,
	SERIAL_BAUD_1800,
	SERIAL_BAUD_2400,
	SERIAL_BAUD_4800,
	SERIAL_BAUD_9600,
	SERIAL_BAUD_19200,
	SERIAL_BAUD_38400,
	SERIAL_BAUD_57600,
	SERIAL_BAUD_115200,

	SERIAL_BAUD_INVALID
} serial_baud_t;

typedef enum {
	SERIAL_STOPBIT_1,
	SERIAL_STOPBIT_2,
} serial_stopbit_t;

typedef enum {
	SERIAL_ERR_OK = 0,

	SERIAL_ERR_SYSTEM,
	SERIAL_ERR_UNKNOWN,
	SERIAL_ERR_INVALID_BAUD,
	SERIAL_ERR_INVALID_BITS,
	SERIAL_ERR_INVALID_PARITY,
	SERIAL_ERR_INVALID_STOPBIT
} serial_err_t;

serial_t*    serial_open	(const char *device);
void         serial_close	(serial_t *h);
void         serial_flush	(const serial_t *h);
serial_err_t serial_setup	(serial_t *h, const serial_baud_t baud, const serial_bits_t bits, const serial_parity_t parity, const serial_stopbit_t stopbit);
serial_err_t serial_write	(const serial_t *h, const void *buffer, unsigned int len);
serial_err_t serial_read	(const serial_t *h, const void *buffer, unsigned int len);

#endif
