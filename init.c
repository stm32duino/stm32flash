/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2010 Geoffrey McRae <geoff@spacevs.com>
  Copyright (C) 2013 Antonio Borneo <borneo.antonio@gmail.com>

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


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "compiler.h"
#include "init.h"
#include "serial.h"
#include "stm32.h"
#include "port.h"
#include "utils.h"

extern FILE *diag;

struct gpio_list {
	struct gpio_list *next;
	int gpio;
	int input; /* 1 if direction of gpio should be changed back to input. */
	int exported; /* 0 if gpio should be unexported. */
};

#if defined(__linux__)
static int write_to(const char *filename, const char *value)
{
	int fd, ret;

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open file \"%s\"\n", filename);
		return 0;
	}
	ret = write(fd, value, strlen(value));
	if (ret < 0) {
		fprintf(stderr, "Error writing in file \"%s\"\n", filename);
		close(fd);
		return 0;
	}
	close(fd);
	return 1;
}

static int read_from(const char *filename, char *buf, size_t len)
{
	int fd, ret;
	size_t n = 0;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open file \"%s\"\n", filename);
		return 0;
	}

	do {
		ret = read(fd, buf + n, len - n);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue; /* try again */
			fprintf(stderr, "Error reading in file \"%s\"\n", filename);
			close(fd);
			return 0;
		}
		n += ret;
	} while (n < len && ret);

	close(fd);
	return n;
}

static int drive_gpio(int n, int level, struct gpio_list **gpio_to_release)
{
	char num[16]; /* sized to carry MAX_INT */
	char file[48]; /* sized to carry longest filename */
	char dir;
	struct stat buf;
	struct gpio_list *new;
	int ret;
	int exported = 1;
	int input = 0;

	sprintf(file, "/sys/class/gpio/gpio%d/value", n);
	ret = stat(file, &buf);
	if (ret) {
		/* file miss, GPIO not exported yet */
		sprintf(num, "%d", n);
		ret = write_to("/sys/class/gpio/export", num);
		if (!ret)
			return 0;
		ret = stat(file, &buf);
		if (ret) {
			fprintf(stderr, "GPIO %d not available\n", n);
			return 0;
		}
		exported = 0;
	}

	sprintf(file, "/sys/class/gpio/gpio%d/direction", n);
	ret = stat(file, &buf);
	if (!ret)
		if (read_from(file, &dir, sizeof(dir)))
			if (dir == 'i')
				input = 1;

	if (exported == 0 || input == 1) {
		new = (struct gpio_list *)malloc(sizeof(struct gpio_list));
		if (new == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 0;
		}
		new->gpio = n;
		new->exported = exported;
		new->input = input;
		new->next = *gpio_to_release;
		*gpio_to_release = new;
	}

	return write_to(file, level ? "high" : "low");
}

static int release_gpio(int n, int input, int exported)
{
	char num[16]; /* sized to carry MAX_INT */
	char file[48]; /* sized to carry longest filename */

	sprintf(num, "%d", n);
	if (input) {
		sprintf(file, "/sys/class/gpio/gpio%d/direction", n);
		write_to(file, "in");
	}
	if (!exported)
		write_to("/sys/class/gpio/unexport", num);

	return 1;
}
#else
static int drive_gpio(int __unused n, int __unused level,
		      struct gpio_list __unused **gpio_to_release)
{
	fprintf(stderr, "GPIO control only available in Linux\n");
	return 0;
}
#endif

static int gpio_sequence(struct port_interface *port, const char *seq, size_t len_seq)
{
	struct gpio_list *gpio_to_release = NULL;
#if defined(__linux__)
	struct gpio_list *to_free;
#endif
	int ret = 0, level, gpio;
	int sleep_time = 0;
	int delimiter = 0;
	const char *sig_str = NULL;
	const char *s = seq;
	size_t l = len_seq;

	fprintf(diag, "\nGPIO sequence start\n");
	while (ret == 0 && *s && l > 0) {
		sig_str = NULL;
		sleep_time = 0;
		delimiter = 0;

		if (*s == '-') {
			level = 0;
			s++;
			l--;
		} else
			level = 1;

		if (isdigit(*s)) {
			gpio = atoi(s);
			while (isdigit(*s)) {
				s++;
				l--;
			}
		} else if (l >= 3 && !strncmp(s, "rts", 3)) {
			sig_str = s;
			gpio = -GPIO_RTS;
			s += 3;
			l -= 3;
		} else if (l >= 3 && !strncmp(s, "dtr", 3)) {
			sig_str = s;
			gpio = -GPIO_DTR;
			s += 3;
			l -= 3;
		} else if (l >= 3 && !strncmp(s, "brk", 3)) {
			sig_str = s;
			gpio = -GPIO_BRK;
			s += 3;
			l -= 3;
		} else if (*s && (l > 0)) {
			delimiter = 1;
			/* The ',' delimiter adds a 100 ms delay between signal toggles.
			 * i.e -rts,dtr will reset rts, wait 100 ms, set dtr.
			 *
			 * The '&' delimiter adds no delay between signal toggles.
			 * i.e -rts&dtr will reset rts and immediately set dtr.
			 *
			 * Example: -rts&dtr,,,rts,-dtr will reset rts and set dtr
			 * without delay, then wait 300 ms, set rts, wait 100 ms, reset dtr.
			 */
			if (*s == ',') {
				s++;
				l--;
				sleep_time = 100000;
			} else if (*s == '&') {
				s++;
				l--;
			} else {
				fprintf(stderr, "Character \'%c\' is not a valid signal or separator\n", *s);
				ret = 1;
				break;
			}
		} else {
			/* E.g. modifier without signal */
			fprintf(stderr, "Invalid sequence %.*s\n", (int) len_seq, seq);
			ret = 1;
			break;
		}

		if (!delimiter) { /* actual gpio/port signal driving */
			if (gpio < 0) {
				gpio = -gpio;
				fprintf(diag, " setting port signal %.3s to %i... ", sig_str, level);
				ret = (port->gpio(port, gpio, level) != PORT_ERR_OK);
				printStatus(diag, ret);
			} else {
				fprintf(diag, " setting gpio %i to %i... ", gpio, level);
				ret = (drive_gpio(gpio, level, &gpio_to_release) != 1);
				printStatus(diag, ret);
			}
		}

		if (sleep_time) {
			fprintf(diag, " delay %i us\n", sleep_time);
			usleep(sleep_time);
		}
	}
#if defined(__linux__)
	while (gpio_to_release) {
		release_gpio(gpio_to_release->gpio, gpio_to_release->input, gpio_to_release->exported);
		to_free = gpio_to_release;
		gpio_to_release = gpio_to_release->next;
		free(to_free);
	}
#endif
	fprintf(diag, "GPIO sequence end\n\n");
	return ret;
}

static int gpio_bl_entry(struct port_interface *port, const char *seq)
{
	char *s;

	if (seq == NULL || seq[0] == ':')
		return 1;

	s = strchr(seq, ':');
	if (s == NULL)
		return gpio_sequence(port, seq, strlen(seq));

	return gpio_sequence(port, seq, s - seq);
}

int gpio_bl_exit(struct port_interface *port, const char *seq)
{
	char *s;

	if (seq == NULL)
		return 1;

	s = strchr(seq, ':');
	if (s == NULL || s[1] == '\0')
		return 1;

	return gpio_sequence(port, s + 1, strlen(s + 1));
}

int init_bl_entry(struct port_interface *port, const char *seq)
{
	if (seq)
		return gpio_bl_entry(port, seq);

	return 0;
}

int init_bl_exit(stm32_t *stm, struct port_interface *port, const char *seq)
{
	if (seq && strchr(seq, ':'))
		return gpio_bl_exit(port, seq);

	return stm32_reset_device(stm);
}
