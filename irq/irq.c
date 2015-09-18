#include "../config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _CAST_
#include "../cpu/68000.h"
#include "../cpu/op68k.h"
#else
#include "../cpu/m68k.h"
#endif

#include "../mmu/mmu.h"

#include "irq.h"

int irq_status=0;

extern int ask_bkpt;

void
set_irq_line(int irq)  {
    uint32 irq_adr;
	IDLE_INIT_FUNC("set_irq_line()");
	IDLE_DEBUG("Called %x %s (status=%x)",irq,get_irq_name(irq),irq_status);
    irq_status|=irq;

    if (irq_status&IRQ2_MASK) {
            irq_adr=(LisaGetWDebugCtxt(4*26,0)<<16)|LisaGetWDebugCtxt(4*26+2,0);
			IDLE_DEBUG("IRQ2 pulse address = %06X",irq_adr);
			m68k_set_irq(M68K_IRQ_2);
	}
	else
	if (irq_status&IRQ1_MASK) {
            irq_adr=(LisaGetWDebugCtxt(4*25,0)<<16)|LisaGetWDebugCtxt(4*25+2,0);
			IDLE_DEBUG("IRQ1 pulse address = %06X",irq_adr);
     	   m68k_set_irq(M68K_IRQ_1);	
		}
	else
     	   m68k_set_irq(M68K_IRQ_NONE);	
	
}

void
reset_irq_line(int irq) {
    int old_status=irq_status;
    int new_level=-1;

	IDLE_INIT_FUNC("reset_irq_line()");
	IDLE_DEBUG("Called %x %s (status=%x)",irq,get_irq_name(irq),irq_status);

	irq_status&=(~irq);
	if ((irq_status&IRQ2_MASK)!=0) 
     {
       IDLE_DEBUG("irq2 again");
       m68k_set_irq(M68K_IRQ_2);
     }
     else
	if (((irq_status&IRQ2_MASK)==0) && 
        ((old_status&IRQ2_MASK)!=0) && 
        ((irq_status&IRQ1_MASK)!=0)  )
     {
       IDLE_DEBUG("irq1 again");
       m68k_set_irq(M68K_IRQ_1);
     }
	else
     	   m68k_set_irq(M68K_IRQ_NONE);	
 }

void
check_irq_line(void)  {
	IDLE_INIT_FUNC("check_irq_line()");
	return;
}

char * 
get_irq_name(int val) {
     switch (val) {
            case VBLIRQ:
                 return "VBL";
            case HDDIRQ:
                 return "HDDVIA";
            case FDIRIRQ:
                 return "FDIR";
            case KEYBIRQ:
                 return "KEYVIA";
            default:
                   return "unknown";
            }
}
