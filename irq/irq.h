#define IRQ1_MASK	0x000F
#define VBLIRQ 		0x0001
#define HDDIRQ		0x0002
#define FDIRIRQ		0x0004

#define IRQ2_MASK	0x00F0
#define KEYBIRQ		0x0010

void
set_irq_line(int irq); 

void
reset_irq_line(int irq); 

void
check_irq_line(void);

char * 
get_irq_name(int val);
