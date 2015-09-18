/*
 * Generic via derived from mame (derived from vice (but not sure...))
 */
#include "../config.h"

#ifdef _CAST_
#include "../cpu/68000.h"
#include "../cpu/op68k.h"
#else
#include "../cpu/m68k.h"
#endif


#include "via.h"
#include "../time/idle_timer_tags.h"
#include "../time/timer.h"

#include <stdio.h>
#include <strings.h>

#define MAX_VIA 8


struct via6522
{
	uint8 in_a;
	uint8 in_ca1;
	uint8 in_ca2;
	uint8 out_a;
	uint8 out_ca2;
	uint8 ddr_a;

	uint8 in_b;
	uint8 in_cb1;
	uint8 in_cb2;
	uint8 out_b;
	uint8 out_cb2;
	uint8 ddr_b;

	uint8 t1cl;
	uint8 t1ch;
	uint8 t1ll;
	uint8 t1lh;
	uint8 t2cl;
	uint8 t2ch;
	uint8 t2ll;
	uint8 t2lh;
	int t1_active;
	int t1_tag;
    double t1_0000; // when did T1 passed by 0 (buggy after a while, should be idle_t)
	int t2_active;
    int t2_tag;
    double t2_0000; // when did T2 passed by 0
    void (*irq_func)(int);
    void (*changed_out_a)(int);
    void (*changed_out_b)(int);
 
    int (*stream_a) (int); // special mode for profile access

	double cycles_to_sec;
	double sec_to_cycles;


	uint8 sr;
	uint8 pcr;
	uint8 acr;
	uint8 ier;
	uint8 ifr;

};

#define	VIA_PB	    0
#define	VIA_PA	    1
#define	VIA_DDRB    2
#define	VIA_DDRA    3
#define	VIA_T1CL    4
#define	VIA_T1CH    5
#define	VIA_T1LL    6
#define	VIA_T1LH    7
#define	VIA_T2CL    8
#define	VIA_T2CH    9
#define	VIA_SR     10
#define	VIA_ACR    11
#define	VIA_PCR    12
#define	VIA_IFR    13
#define	VIA_IER    14
#define	VIA_PANH   15

#define V_CYCLES_TO_TIME(c) ((double)(c) * v->cycles_to_sec)
#define V_TIME_TO_CYCLES(t) ((int)((t) * v->sec_to_cycles))

/* Macros for PCR */
#define CA1_LOW_TO_HIGH(c)		(c & 0x01)
#define CA1_HIGH_TO_LOW(c)		(!(c & 0x01))

#define CB1_LOW_TO_HIGH(c)		(c & 0x10)
#define CB1_HIGH_TO_LOW(c)		(!(c & 0x10))

#define CA2_INPUT(c)			(!(c & 0x08))
#define CA2_LOW_TO_HIGH(c)		((c & 0x0c) == 0x04)
#define CA2_HIGH_TO_LOW(c)		((c & 0x0c) == 0x00)
#define CA2_IND_IRQ(c)			((c & 0x0a) == 0x02)

#define CA2_OUTPUT(c)			(c & 0x08)
#define CA2_AUTO_HS(c)			((c & 0x0c) == 0x08)
#define CA2_HS_OUTPUT(c)		((c & 0x0e) == 0x08)
#define CA2_PULSE_OUTPUT(c)		((c & 0x0e) == 0x0a)
#define CA2_FIX_OUTPUT(c)		((c & 0x0c) == 0x0c)
#define CA2_OUTPUT_LEVEL(c)		((c & 0x02) >> 1)

#define CB2_INPUT(c)			(!(c & 0x80))
#define CB2_LOW_TO_HIGH(c)		((c & 0xc0) == 0x40)
#define CB2_HIGH_TO_LOW(c)		((c & 0xc0) == 0x00)
#define CB2_IND_IRQ(c)			((c & 0xa0) == 0x20)

#define CB2_OUTPUT(c)			(c & 0x80)
#define CB2_AUTO_HS(c)			((c & 0xc0) == 0x80)
#define CB2_HS_OUTPUT(c)		((c & 0xe0) == 0x80)
#define CB2_PULSE_OUTPUT(c)		((c & 0xe0) == 0xa0)
#define CB2_FIX_OUTPUT(c)		((c & 0xc0) == 0xc0)
#define CB2_OUTPUT_LEVEL(c)		((c & 0x20) >> 5)

/* Macros for ACR */
#define PA_LATCH_ENABLE(c)		(c & 0x01)
#define PB_LATCH_ENABLE(c)		(c & 0x02)

#define SR_DISABLED(c)			(!(c & 0x1c))
#define SI_T2_CONTROL(c)		((c & 0x1c) == 0x04)
#define SI_O2_CONTROL(c)		((c & 0x1c) == 0x08)
#define SI_EXT_CONTROL(c)		((c & 0x1c) == 0x0c)
#define SO_T2_RATE(c)			((c & 0x1c) == 0x10)
#define SO_T2_CONTROL(c)		((c & 0x1c) == 0x14)
#define SO_O2_CONTROL(c)		((c & 0x1c) == 0x18)
#define SO_EXT_CONTROL(c)		((c & 0x1c) == 0x1c)

#define T1_SET_PB7(c)			(c & 0x80)
#define T1_CONTINUOUS(c)		(c & 0x40)
#define T2_COUNT_PB6(c)			(c & 0x20)

/* Interrupt flags */
#define INT_CA2	0x01
#define INT_CA1	0x02
#define INT_SR	0x04
#define INT_CB2	0x08
#define INT_CB1	0x10
#define INT_T2	0x20
#define INT_T1	0x40
#define INT_ANY	0x80

#define CLR_PA_INT(v, which)	via_clear_int (which, INT_CA1 | ((!CA2_IND_IRQ(v->pcr)) ? INT_CA2: 0))
#define CLR_PB_INT(v, which)	via_clear_int (which, INT_CB1 | ((!CB2_IND_IRQ(v->pcr)) ? INT_CB2: 0))

#define TIMER1_VALUE(v) (v->t1ll+(v->t1lh<<8))
#define TIMER2_VALUE(v) (v->t2ll+(v->t2lh<<8))

static struct via6522 via[MAX_VIA];

static void via_set_int (int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_int()");
    uint8 old=v->ifr;
	v->ifr |= (data&0x7F);
	IDLE_DEBUG("6522VIA chip %d: IFR = %02X", which, v->ifr);

	if (v->ier & v->ifr)
    {
	v->ifr |= INT_ANY;
		if ((old&INT_ANY)==0x00)
		   if ((v->irq_func)!=NULL) v->irq_func(IRQ_SET);
    }
}

static void via_t1_timeout(int which) {
	struct via6522 *v = via + which;
    IDLE_INIT_FUNC("via_t1_timeout()");       
    IDLE_DEBUG("Called chip %d",which);
    	if (T1_CONTINUOUS (v->acr))
    {
		if (T1_SET_PB7(v->acr))
			v->out_b ^= 0x80;
        // T1 continious : rearm timer
        		{
         idle_time delta;
         int timer=TIMER1_VALUE(v);
         delta=double2time(V_CYCLES_TO_TIME(timer));	
         // create timer T1
         idle_timer_remove(v->t1_tag);
         IDLE_DEBUG("rearm continious timer T1 val=%04x",TIMER1_VALUE(v));
         idle_timer_create(via_t1_timeout,which,v->t1_tag,delta);       
		 v->t1_active = 1;
        }

    }
	else
    {
		if (T1_SET_PB7(v->acr))
			v->out_b |= 0x80;
		v->t1_active = 0;
		v->t1_0000=idle_get_time_double();
    }
    
	if ((v->ddr_b) && T1_SET_PB7(v->acr))
	{
		uint8 write_data = (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
     	if (v->changed_out_b!=NULL) (*v->changed_out_b)(write_data); 
	}

	if (!(v->ifr & INT_T1))
		via_set_int (which, INT_T1);

}

static void via_t2_timeout(int which) {
	struct via6522 *v = via + which;
    IDLE_INIT_FUNC("via_t2_timeout()");  
    IDLE_DEBUG("Called chip %d",which);
   	v->t2_active = 0;

	if (!(v->ifr & INT_T2))
		via_set_int (which, INT_T2);

    v->t2_0000=idle_get_time_double();
     
}

static void via_clear_int (int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_clear_int()");


	v->ifr = (v->ifr & ~data) & 0x7f;
	IDLE_DEBUG("6522VIA chip %d: IFR = x%02x data= x%02x", which, v->ifr,data);

	if (v->ifr & v->ier)
		v->ifr |= INT_ANY;
	else
	{
		if ((v->irq_func)!=NULL) v->irq_func(IRQ_RESET);
	}
}

void via_init (int which, int tag_t1, int tag_t2,void (*irq_func)(int)) {
	struct via6522 *v = via + which;
    v->t1_tag=tag_t1;
    v->t2_tag=tag_t2;     
    v->irq_func=irq_func;
    v->changed_out_a=NULL;
    v->changed_out_b=NULL;
    v->stream_a=NULL;
}

void
via_set_output_callback(int which,void (*changed_out_a)(int),void (*changed_out_b)(int)) {
	struct via6522 *v = via + which;
    v->changed_out_a=changed_out_a;
    v->changed_out_b=changed_out_b;
}

void
via_set_stream_a_callback(int which,int (*stream_a)(int)) {
	struct via6522 *v = via + which;
    v->stream_a=stream_a;
}

void via_set_clock(int which,int clock)
{
	via[which].sec_to_cycles = clock;
	via[which].cycles_to_sec = 1.0 / via[which].sec_to_cycles;
}


void via_reset(void)
{
	int i;
	struct via6522 v;
	IDLE_INIT_FUNC("via_reset()");

	memset(&v, 0, sizeof(v));

	for (i = 0; i < MAX_VIA; i++)
    {
		v.t1ll = via[i].t1ll;
		v.t1lh = via[i].t1lh;
		v.t2ll = via[i].t2ll;
		v.t2lh = via[i].t2lh;
		via[i] = v;
    }
}

/******************* CPU interface for VIA read *******************/

int via_read(int which, int offset)
{
	struct via6522 *v = via + which;
	int val = 0;
	IDLE_INIT_FUNC("via_read()");

	offset &= 0xf;

	switch (offset)
    {
    case VIA_PB:
		/* update the input */
		if (PB_LATCH_ENABLE(v->acr) == 0)
		{
		}

		CLR_PB_INT(v, which);

		/* combine input and output values, hold DDRB bit 7 high if T1_SET_PB7 */
		if (T1_SET_PB7(v->acr))
			val = (v->out_b & (v->ddr_b | 0x80)) | (v->in_b & ~(v->ddr_b | 0x80));
		else
			val = (v->out_b & v->ddr_b) + (v->in_b & ~v->ddr_b);

        IDLE_DEBUG("PB chip %d =0x%x",which,val);
		break;

    case VIA_PA:
		/* update the input */
		if (PA_LATCH_ENABLE(v->acr) == 0)
		{
		}

        if ((v->stream_a)!=NULL) 
           v->in_a=(*v->stream_a)(0);

		/* combine input and output values */
//		val = (v->out_a & v->ddr_a) + (v->in_a & ~v->ddr_a);
		val = (0xFF & v->ddr_a) + (v->in_a & ~v->ddr_a);

        IDLE_DEBUG("PA chip %d =0x%x",which,val);

		CLR_PA_INT(v, which);

		/* If CA2 is configured as output and in pulse or handshake mode,
		   CA2 is set now */
		if (CA2_AUTO_HS(v->pcr))
		{
			if (v->out_ca2)
			{
				/* set CA2 */
				v->out_ca2 = 0;
			}
		}

		break;

    case VIA_PANH:
		/* update the input */
		if (PA_LATCH_ENABLE(v->acr) == 0)
		{
		}

        if ((v->stream_a)!=NULL) 
           v->in_a=(*v->stream_a)(0);

		/* combine input and output values */
//		val = (v->out_a & v->ddr_a) + (v->in_a & ~v->ddr_a);
		val = (0xFF & v->ddr_a) + (v->in_a & ~v->ddr_a);

        IDLE_DEBUG("PA (NH) chip %d =0x%x",which,val);

		break;

    case VIA_DDRB:
		val = v->ddr_b;
		break;

    case VIA_DDRA:
		val = v->ddr_a;
		break;

    case VIA_T1CL:
		via_clear_int (which, INT_T1);
		if (v->t1_active) {
			val = (V_TIME_TO_CYCLES(idle_timer_get_left_time(v->t1_tag)))&0xFF; // test me
            IDLE_DEBUG("VIA%d Get T1L=x%02x cnt=x%04x/x%04x",
            which,
            val,
            V_TIME_TO_CYCLES(idle_timer_get_left_time(v->t1_tag)),
            TIMER1_VALUE(v));
        }
		else
		{
			           val = ((0x10000-
					   (V_TIME_TO_CYCLES(idle_get_time_double()-v->t1_0000)&0xffff)
					   -1)&0xff);
                       IDLE_DEBUG("VIA%d Get T1L=%d inactive",which,val);
		}
		break;

    case VIA_T1CH:
		if (v->t1_active) {
			val = ((V_TIME_TO_CYCLES(idle_timer_get_left_time(v->t1_tag)))&0xFF00)>>8; // test me
            IDLE_DEBUG("VIA%d Get T1H=%d",which,val);
        }
		else
		{
			           val = ((0x10000-
					   (V_TIME_TO_CYCLES(idle_get_time_double()-v->t1_0000)&0xffff)
					   -1)&0xff00)>>8;
                       IDLE_DEBUG("VIA%d Get T1H=%d inactive",which,val);
		}
		break;

    case VIA_T1LL:
		val = v->t1ll;
        IDLE_DEBUG("VIA%d Get T1LL=%d",which,val);
		break;

    case VIA_T1LH:
		val = v->t1lh;
        IDLE_DEBUG("VIA%d Get T1LH=%d",which,val);
		break;

    case VIA_T2CL:
		via_clear_int (which, INT_T2);
		if (v->t2_active) {
  // here t2 elapsed L
			val = ((V_TIME_TO_CYCLES(idle_timer_get_left_time(v->t2_tag)))&0xFF); // test me
                       IDLE_DEBUG("VIA%d Get T2L=%d",which,val);
         }
		else
		{
			if (T2_COUNT_PB6(v->acr))
			{
				val = v->t2cl;
                IDLE_DEBUG("VIA%d Get T2L=x%02x in PB6 mode",which,val);
			}
			else
			{
                // here count from FFFF to 0000 repeatedly (after date t2_0000)
			           val = (0x10000-
					   (V_TIME_TO_CYCLES(idle_get_time_double()-v->t2_0000)&0xffff)
					   -1)&0xff;
                       IDLE_DEBUG("VIA%d Get T2L=%d inactive",which,val);
			}
		}
		break;

    case VIA_T2CH:
		if (v->t2_active) {
			val = ((V_TIME_TO_CYCLES(idle_timer_get_left_time(v->t2_tag)))&0xFF00)>>8; // test me
                       IDLE_DEBUG("VIA%d Get T2H=%d",which,val);
        }
		else
		{
			if (T2_COUNT_PB6(v->acr))
			{
				val = v->t2ch;
                IDLE_DEBUG("VIA%d Get T2H=x%02x in PB6 mode",which,val);
			}
			else
			{
			           val = ((0x10000-
					   (V_TIME_TO_CYCLES(idle_get_time_double()-v->t2_0000)&0xffff)
					   -1)&0xff00)>>8;
                       IDLE_DEBUG("VIA%d Get T2H=%d inactive",which,val);
			}
		}
		break;

    case VIA_SR:
		val = v->sr;
		break;

    case VIA_PCR:
		val = v->pcr;
		break;

    case VIA_ACR:
		val = v->acr;
		break;

    case VIA_IER:
		val = v->ier | 0x80;
		break;

    case VIA_IFR:
		val = v->ifr;
		break;
    }
	return val; 
}
void via_write(int which, int offset, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_write()");

	offset &=0x0f;

	switch (offset)
    {
    case VIA_PB:
         IDLE_DEBUG("Chip %d Set PB=x%x",which,data);
		if (T1_SET_PB7(v->acr))
			v->out_b = (v->out_b & 0x80) | (data  & 0x7f);
		else
			v->out_b = data;

		if (v->ddr_b)
		{
			uint8 write_data = (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
			if (v->changed_out_b!=NULL) (*v->changed_out_b)(write_data); 
		}

		CLR_PB_INT(v, which);

		/* If CB2 is configured as output and in pulse or handshake mode,
		   CB2 is set now */
		if (CB2_AUTO_HS(v->pcr))
		{
			if (v->out_cb2)
			{
				/* set CB2 */
				v->out_cb2 = 0;
			}
		}
		break;

    case VIA_PA:
         IDLE_DEBUG("Chip %d Set PA=x%x",which,data);

        // handle stream out (ie profile write for idle)
        if ((v->stream_a)!=NULL) 
           (*v->stream_a)(data);


		v->out_a = data;

		if (v->ddr_a)
		{
			uint8 write_data = (v->out_a & v->ddr_a) | (v->ddr_a ^ 0xff);
			if (v->changed_out_a!=NULL) (*v->changed_out_a)(write_data); 
		}

		CLR_PA_INT(v, which);

		/* If CA2 is configured as output and in pulse or handshake mode,
		   CA2 is set now */
		if (CA2_PULSE_OUTPUT(v->pcr))
		{

			/* set CA2 (shouldn't be needed) */
			v->out_ca2 = 1;
		}
		else if (CA2_AUTO_HS(v->pcr))
		{
			if (v->out_ca2)
			{
				/* set CA2 */
				v->out_ca2 = 0;

			}
		}

		break;

    case VIA_PANH:
		v->out_a = data;

		if (v->ddr_a)
		{
			uint8 write_data = (v->out_a & v->ddr_a) | (v->ddr_a ^ 0xff);
			if (v->changed_out_a!=NULL) (*v->changed_out_a)(write_data); 
		}

		break;

    case VIA_DDRB:
    	if ( data != v->ddr_b )
    	{
			v->ddr_b = data;

			{
				uint8 write_data = (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
    			if (v->changed_out_b!=NULL) (*v->changed_out_b)(write_data); 
			}
		}
		break;

    case VIA_DDRA:
    	if ( data != v->ddr_a )
    	{
			v->ddr_a = data;

			{
				uint8 write_data = (v->out_a & v->ddr_a) | (v->ddr_a ^ 0xff);
    			if (v->changed_out_a!=NULL) (*v->changed_out_a)(write_data); 
			}
		}
		break;

    case VIA_T1CL:
    case VIA_T1LL:
		v->t1ll = data;
	    IDLE_DEBUG("VIA%d SET T1CL/T1LL = %02X", which, data);
		break;

	case VIA_T1LH:
	    v->t1lh = data;
	    IDLE_DEBUG("VIA%d SET T1LH = %02X", which, data);
	    via_clear_int (which, INT_T1);
	    break;

    case VIA_T1CH:
		IDLE_DEBUG("VIA%d SET T1CH = %02X", which, data);
		v->t1ch = v->t1lh = data;
		v->t1cl = v->t1ll;

		via_clear_int (which, INT_T1);

		if (T1_SET_PB7(v->acr))
		{
			v->out_b &= 0x7f;

			{
				uint8 write_data = (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
    			if (v->changed_out_b!=NULL) (*v->changed_out_b)(write_data); 
			}
		}
		{
         idle_time delta;
         int timer=TIMER1_VALUE(v);
         delta=double2time(V_CYCLES_TO_TIME(timer));	
         // create timer T1
         idle_timer_remove(v->t1_tag);
         IDLE_DEBUG("chip %d create timer T1 x%04x",which,TIMER1_VALUE(v));
         idle_timer_create(via_t1_timeout,which,v->t1_tag,delta);       
		 v->t1_active = 1;
        }
		break;

    case VIA_T2CL:
		v->t2ll = data;
	    IDLE_DEBUG("VIA%d SET T2LH = %02X", which, data);
		break;

    case VIA_T2CH:
		IDLE_DEBUG("VIA%d SET T2CH = %02X", which, data);
		v->t2ch = v->t2lh = data;
		v->t2cl = v->t2ll;

		via_clear_int (which, INT_T2);

		if (!T2_COUNT_PB6(v->acr))
		{
           // create timer T2 if greater than 0
           if (TIMER2_VALUE(v)>0)
		   {
              idle_time delta;
              int timer=TIMER2_VALUE(v);
              delta=double2time(V_CYCLES_TO_TIME(timer));	
              // create timer T2
              IDLE_DEBUG("VIA%d create timer T2 x%04x",which,TIMER2_VALUE(v));
              idle_timer_remove(v->t2_tag);
              idle_timer_create(via_t2_timeout,which,v->t2_tag,delta);       
		      v->t2_active = 1;
           }
           else {
                // just mark date so we can read correct values
                IDLE_DEBUG("VIA%d T2 zero get time",which);
                v->t2_active=0;
                v->t2_0000=idle_get_time_double();
           }
		}
		else // of not T2countPB6
		{
             IDLE_DEBUG("VIA%d Begin count PB6 x%x",which,TIMER2_VALUE(v));
		}
		break;

    case VIA_SR:
		v->sr = data;
		if (SO_O2_CONTROL(v->acr))
		{
		}

		if (SO_EXT_CONTROL(v->acr))
		{
		}
		break;

    case VIA_PCR:
		v->pcr = data;
		IDLE_DEBUG("VIA%d PCR = %02X", which, data);

		if (CA2_FIX_OUTPUT(data) && CA2_OUTPUT_LEVEL(data) ^ v->out_ca2)
		{
			v->out_ca2 = CA2_OUTPUT_LEVEL(data);
		}

		if (CB2_FIX_OUTPUT(data) && CB2_OUTPUT_LEVEL(data) ^ v->out_cb2)
		{
			v->out_cb2 = CB2_OUTPUT_LEVEL(data);
		}
		break;

    case VIA_ACR:
		v->acr = data;
		IDLE_DEBUG("VIA%d ACR = %02X", which, data);
		if (T1_SET_PB7(v->acr))
		{
			if (v->t1_active)
				v->out_b &= ~0x80;
			else
				v->out_b |= 0x80;

			{
				uint8 write_data = (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
    			if (v->changed_out_b!=NULL) (*v->changed_out_b)(write_data); 
			}
		}
		if (T1_CONTINUOUS(data))
		{
			v->t1_active = 1;
           IDLE_DEBUG("WARN T1 should be created...");
		}
		if (SI_EXT_CONTROL(data))
		{
		}
		break;

	case VIA_IER:
		if (data & 0x80)
			v->ier |= data & 0x7f;
		else
			v->ier &= ~(data & 0x7f);

		if (v->ifr & INT_ANY)
		{
			if (((v->ifr & v->ier) & 0x7f) == 0)
			{
				v->ifr &= ~INT_ANY;
			}
		}
		else
		{
			if ((v->ier & v->ifr) & 0x7f)
			{
				v->ifr |= INT_ANY;
			}
		}
		break;

	case VIA_IFR:
		if (data & INT_ANY)
			data = 0x7f;
		via_clear_int (which, data);
		break;
    }
}

void via_set_input_a(int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_input_a()");

	v->in_a = data;
}

void via_set_input_ca1(int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_input_ca1()");

	/* limit the data to 0 or 1 */
	data = data ? 1 : 0;

	/* handle the active transition */
	if (data != v->in_ca1)
    {
		IDLE_DEBUG("6522VIA chip %d: CA1 = %02X\n", which, data);
		if ((CA1_LOW_TO_HIGH(v->pcr) && data) || (CA1_HIGH_TO_LOW(v->pcr) && !data))
		{
			if (PA_LATCH_ENABLE(v->acr))
			{
			}

			via_set_int (which, INT_CA1);

			/* CA2 is configured as output and in pulse or handshake mode,
			   CA2 is cleared now */
			if (CA2_AUTO_HS(v->pcr))
			{
				if (!v->out_ca2)
				{
					/* clear CA2 */
					v->out_ca2 = 1;

					/* call the CA2 output function */
				}
			}
		}

		v->in_ca1 = data;
    }
}

void via_set_input_ca2(int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_input_ca2()");

	/* limit the data to 0 or 1 */
	data = data ? 1 : 0;

	/* CA2 is in input mode */
	if (CA2_INPUT(v->pcr))
    {
		/* the new state has caused a transition */
		if (v->in_ca2 != data)
		{
			/* handle the active transition */
			if ((data && CA2_LOW_TO_HIGH(v->pcr)) || (!data && CA2_HIGH_TO_LOW(v->pcr)))
			{
				/* mark the IRQ */
				via_set_int (which, INT_CA2);
			}
			/* set the new value for CA2 */
			v->in_ca2 = data;
		}
    }


}

void via_set_input_b(int which, int data)
{
	struct via6522 *v = via + which;
    uint8 old_b=v->in_b;
    uint16 count=TIMER2_VALUE(v);
	IDLE_INIT_FUNC("via_set_input_b()");
	v->in_b = data;
	// we count negative transition of PB6
	if (T2_COUNT_PB6(v->acr) && (count>0)) {
       if (((data&0x40)==0) && ((old_b&0x40)==0x40)) {
          count--;
          IDLE_DEBUG("6522 VIA chip %d count %x",which,count);
          v->t2cl=count&0xFF;
          v->t2ch=(count&0xFF00)>>8;
          if (count==0) {
             IDLE_TRACE("6522 VIA chip %d BINGO PB6",which);
	         if (!(v->ifr & INT_T2))
		        via_set_int (which, INT_T2);
          }
       }
    }
}

void via_set_input_cb1(int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_input_cb1()");

	/* limit the data to 0 or 1 */
	data = data ? 1 : 0;

	/* handle the active transition */
	if (data != v->in_cb1)
    {
		if ((CB1_LOW_TO_HIGH(v->pcr) && data) || (CB1_HIGH_TO_LOW(v->pcr) && !data))
		{
			if (PB_LATCH_ENABLE(v->acr))
			{
			}

			via_set_int (which, INT_CB1);

			/* CB2 is configured as output and in pulse or handshake mode,
			   CB2 is cleared now */
			if (CB2_AUTO_HS(v->pcr))
			{
				if (!v->out_cb2)
				{
					/* clear CB2 */
					v->out_cb2 = 1;
				}
			}
		}
		v->in_cb1 = data;
    }
}

void via_set_input_cb2(int which, int data)
{
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_set_input_cb2()");

	/* limit the data to 0 or 1 */
	data = data ? 1 : 0;

	/* CB2 is in input mode */
	if (CB2_INPUT(v->pcr))
    {
		/* the new state has caused a transition */
		if (v->in_cb2 != data)
		{
			/* handle the active transition */
			if ((data && CB2_LOW_TO_HIGH(v->pcr)) || (!data && CB2_HIGH_TO_LOW(v->pcr)))
			{
				/* mark the IRQ */
				via_set_int (which, INT_CB2);
			}
			/* set the new value for CB2 */
			v->in_cb2 = data;
		}
    }
}

uint8 via_get_output_a(int which) {
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_get_output_a()");
    if (v->ddr_a)
       IDLE_DEBUG("out=%x ddra=%x",v->out_a,v->ddr_a);
       
    return (v->out_a & v->ddr_a) | (v->ddr_a ^ 0xff);
}

uint8 via_get_output_b(int which) {
	struct via6522 *v = via + which;
	IDLE_INIT_FUNC("via_get_output_b()");

    return (v->out_b & v->ddr_b) | (v->ddr_b ^ 0xff);
}

