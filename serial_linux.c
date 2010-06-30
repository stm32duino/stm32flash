#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "serial.h"

struct serial {
	int			fd;
	struct termios		oldtio;
	struct termios		newtio;

	serial_baud_t		baud;
	serial_bits_t		bits;
	serial_parity_t		parity;
	serial_stopbit_t	stopbit;
};

serial_t* serial_open(const char *device) {
	serial_t *h = calloc(sizeof(serial_t), 1);

	h->fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (h->fd < 0) {
		free(h);
		return NULL;
	}
	fcntl(h->fd, F_SETFL, 0);

	tcgetattr(h->fd, &h->oldtio);
	tcgetattr(h->fd, &h->newtio);

	h->newtio.c_cc[VMIN ] = 0;
	h->newtio.c_cc[VTIME] = 20;

	return h;
}

void serial_close(serial_t *h) {
	serial_flush(h);
	tcsetattr(h->fd, TCSANOW, &h->oldtio);
	close(h->fd);
	free(h);
}

void serial_flush(const serial_t *h) {
	tcflush(h->fd, TCIFLUSH);
}

serial_err_t serial_setup(serial_t *h, const serial_baud_t baud, const serial_bits_t bits, const serial_parity_t parity, const serial_stopbit_t stopbit) {
	speed_t		port_baud;
	tcflag_t	port_bits;
	tcflag_t	port_parity;
	tcflag_t	port_stop;

	switch(baud) {
		case SERIAL_BAUD_1200  : port_baud = B1200  ; break;
		case SERIAL_BAUD_1800  : port_baud = B1800  ; break;
		case SERIAL_BAUD_2400  : port_baud = B2400  ; break;
		case SERIAL_BAUD_4800  : port_baud = B4800  ; break;
		case SERIAL_BAUD_9600  : port_baud = B9600  ; break;
		case SERIAL_BAUD_19200 : port_baud = B19200 ; break;
		case SERIAL_BAUD_38400 : port_baud = B38400 ; break;
		case SERIAL_BAUD_57600 : port_baud = B57600 ; break;
		case SERIAL_BAUD_115200: port_baud = B115200; break;

		case SERIAL_BAUD_INVALID:
		default:
			return SERIAL_ERR_INVALID_BAUD;
	}

	switch(bits) {
		case SERIAL_BITS_5: port_bits = CS5; break;
		case SERIAL_BITS_6: port_bits = CS6; break;
		case SERIAL_BITS_7: port_bits = CS7; break;
		case SERIAL_BITS_8: port_bits = CS8; break;

		default:
			return SERIAL_ERR_INVALID_BITS;
	}

	switch(parity) {
		case SERIAL_PARITY_NONE: port_parity = 0;               break;
		case SERIAL_PARITY_EVEN: port_parity = PARENB;          break;
		case SERIAL_PARITY_ODD : port_parity = PARENB | PARODD; break;

		default:
			return SERIAL_ERR_INVALID_PARITY;
	}

	switch(stopbit) {
		case SERIAL_STOPBIT_1: port_stop = 0;	   break;
		case SERIAL_STOPBIT_2: port_stop = CSTOPB; break;

		default:
			return SERIAL_ERR_INVALID_STOPBIT;
	}

	/* if the port is already configured, no need to do anything */
	if (
		h->baud	   == baud   &&
		h->bits	   == bits   &&
		h->parity  == parity &&
		h->stopbit == stopbit
	) return SERIAL_ERR_OK;

	/* reset the settings */
	cfmakeraw(&h->newtio);
	h->newtio.c_cflag &= ~CS8;

	/* setup the new settings */
	cfsetispeed(&h->newtio, port_baud);
	cfsetospeed(&h->newtio, port_baud);
	h->newtio.c_cflag |= port_parity;
	h->newtio.c_cflag |= port_bits;
	h->newtio.c_iflag |= port_stop;

	/* set the settings */
	serial_flush(h);
	if (tcsetattr(h->fd, TCSANOW, &h->newtio) != 0)
		return SERIAL_ERR_SYSTEM;

	/* confirm they were set */
	struct termios settings;
	tcgetattr(h->fd, &settings);
	if (
		settings.c_iflag != h->newtio.c_iflag ||
		settings.c_oflag != h->newtio.c_oflag ||
		settings.c_cflag != h->newtio.c_cflag ||
		settings.c_lflag != h->newtio.c_lflag
	)	return SERIAL_ERR_UNKNOWN;

	h->baud	   = baud;
	h->bits	   = bits;
	h->parity  = parity;
	h->stopbit = stopbit;
	return SERIAL_ERR_OK;
}

serial_err_t serial_write(const serial_t *h, const void *buffer, unsigned int len) {
	ssize_t r;
	uint8_t *pos = (uint8_t*)buffer;

	while(len > 0) {
		r = write(h->fd, pos, len);
		if (r < 0) return SERIAL_ERR_SYSTEM;

		len -= r;
		pos += r;
	}

	return SERIAL_ERR_OK;
}

serial_err_t serial_read(const serial_t *h, const void *buffer, unsigned int len) {
	ssize_t r;
	uint8_t *pos = (uint8_t*)buffer;

	while(len > 0) {
		r = read(h->fd, pos, len);
		if (r < 0) return SERIAL_ERR_SYSTEM;

		len -= r;
		pos += r;
	}

	return SERIAL_ERR_OK;
}

