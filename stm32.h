#ifndef _STM32_H
#define _STM32_H

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

const stm32_dev_t devices[] = {
	{0x412, "Low-density"      , 0x20000200, 0x20002800, 0x08000000, 0x08008000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x410, "Medium-density"   , 0x20000200, 0x20005000, 0x08000000, 0x08020000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x414, "High-density"     , 0x20000200, 0x20010000, 0x08000000, 0x08080000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x418, "Connectivity line", 0x20000200, 0x20010000, 0x08000000, 0x08040000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFB000, 0x1FFFF800},
	{0x420, "Medium-density VL", 0x20000200, 0x20002000, 0x08000000, 0x08020000, 4, 1024, 0x1FFFF800, 0x1FFFF80F, 0x1FFFF000, 0x1FFFF800},
	{0x430, "XL-density"       , 0x20000800, 0x20018000, 0x08000000, 0x08100000, 2, 2048, 0x1FFFF800, 0x1FFFF80F, 0x1FFFE000, 0x1FFFF800},
	{0x000}
};

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
	uint8_t			bl_version;
	uint8_t			version;
	uint8_t			option1, option2;
	uint16_t		pid;
	stm32_cmd_t		cmd;
	const stm32_dev_t	*dev;
} stm32_t;

#endif
