
SUBDIRS = parsers

bin_PROGRAMS = stm32flash

AUTOMAKE_OPTIONS = subdir-objects


stm32flash_SOURCES  = \
	dev_table.c	\
	i2c.c		\
	init.c		\
	main.c		\
	port.c		\
	serial_common.c	\
	serial_platform.c\
	stm32.c		\
	utils.c

stm32flash_LDADD   = ${top_builddir}/parsers/parsers.la

stm32flash_CFLAGS = \
  -g3 \
  -Os \
  -Wall \
  -Wextra  \
  -I$(srcdir)/parsers


stm32flash_LDFLAGS = \
  -Wall \
  -g3 \
  -Wextra

all:


.PHONY: all
.SILENT: all
