#include <stdint.h>

#define SCS_BASE		((uint32_t)0xE000E000)
#define SCB_BASE		(SCS_BASE + 0x0D00)
#define SCB			((SCB_Type *) SCB_BASE)
#define NVIC_SYSRESETREQ	2
#define NVIC_AIRCR_VECTKEY	(0x5FA << 16)

typedef struct {
  volatile uint32_t const CPUID;
  volatile uint32_t ICSR;
  volatile uint32_t VTOR;
  volatile uint32_t AIRCR;
  volatile uint32_t SCR;
  volatile uint32_t CCR;
  volatile uint32_t SHPR[3];
  volatile uint32_t SHCSR;
  volatile uint32_t CFSR;
  volatile uint32_t HFSR;
  volatile uint32_t DFSR;
  volatile uint32_t MMFAR;
  volatile uint32_t BFAR;
  volatile uint32_t AFSR;
} SCB_Type;

void main() {
	/* generate a system reset request and wait */
	SCB->AIRCR = (NVIC_AIRCR_VECTKEY | (SCB->AIRCR & (0x700)) | (1<<NVIC_SYSRESETREQ)); 
	while(1);
}
