#include <stdlib.h>
#include "../config.h"

#include "../cpu/m68k.h"

// to register io functions
#include "../mmu/mmu.h"
#include "../time/timer.h"
#include "../time/idle_timer_tags.h"
#include "via.h"
#include "../irq/irq.h"

// pin assignement is as follow
// 
// PA0 - PA7 : COP421
//
// PB0 : reset keyboard (spec83) or record sound (spec81)
// PB0 is reset keyboard => enable to force low for a period
// PB1-3 : speaker volume
// PB4 : floppy dir interrupt status
// PB5 : FDIR for disk
// PB6 : 
// PB7 : reset profile
//
// CA1 : COPS421 S0
// CA2 : COPS421 S1
//
// T2 and SR used for sound (CVSD)
// CB1 : out clock to sound (CVSD)

// for pc debug
#ifdef _CAST_
#include "../cpu/68000.h"
#include "../cpu/op68k.h"
#else
#include "../cpu/m68k.h"
#endif


// the mapping address of this VIA
#define VIABASE 0xDD81
// the increment value
#define VIA_INC 2

// don't know why but this must be init to 0 (for now)
uint8 via_keyb_irb=0x00;
uint8 via_keyb_ira;


#define FIFO_SIZE 32
uint8 fifo[FIFO_SIZE];
int fifo_size=0;


int mouse_enable=0;

int key_can_read=1;

// simple COP internal FIFO
int push_fifo(uint8 val) {
    if (fifo_size>=FIFO_SIZE) return 1;
    fifo[fifo_size]=val;
    fifo_size++;
    return 0;
}

int pop_fifo(uint8* val) {
    int i;
    if (fifo_size==0) return 1;
    *val=fifo[0];
    for (i=0;i<fifo_size-1;i++) fifo[i]=fifo[i+1];
    fifo_size--;
    return 0;
}

void clean_fifo(void) {
     fifo_size=0;
}

int sizeleft_fifo(void) {
    return FIFO_SIZE-fifo_size;
}


// timeout, cop data not read
void via_send_COP2VIA_acq(int param) {
    IDLE_INIT_FUNC("via_send_COP2VIA_acq()");
    IDLE_TRACE("event lost");
    via_set_input_ca1(KEYB_VIA, 0);
    via_set_stream_a_callback(KEYB_VIA,NULL);
    key_can_read=1;
}

int via_code_read(int dummy) {
    int val;
    idle_time delta;
    uint8 data;
    IDLE_INIT_FUNC("via_code_read()");
    IDLE_DEBUG("called");
    idle_timer_remove(COP_KEYB2);
    via_set_stream_a_callback(KEYB_VIA,NULL);
    // port A was read, acknoledge it
    via_set_input_ca1(KEYB_VIA, 0);
    if (fifo_size>0) {
       IDLE_DEBUG("Still %d events in fifo",fifo_size);
    }
    key_can_read=1;
    return via_keyb_ira;
}

int via_send_COP2VIA(uint8 code) {
    idle_time delta;
    IDLE_INIT_FUNC("via_send_COP2VIA()");
    IDLE_DEBUG("called code=x%x",code);
    key_can_read=0;
    via_keyb_ira=code;
    via_set_input_a(KEYB_VIA,via_keyb_ira);
    // request VIA Port A read
    via_set_input_ca1(KEYB_VIA, 1);

    delta=double2time(1.0);	
    idle_timer_remove(COP_KEYB2);
    IDLE_DEBUG("create timer COP");
    // set up a timer and a hook when PA is read
    idle_timer_create(via_send_COP2VIA_acq,0,COP_KEYB2,delta);       
    via_set_stream_a_callback(KEYB_VIA,via_code_read);
    return 0;
}

// code is already in lisa's scancode
int via_keyb_event_key(uint8 code)
{
    IDLE_INIT_FUNC("via_keyb_event_key()");
    IDLE_DEBUG("keyb event x%02x",code);
     
    push_fifo(code);
     
    return 0;
}
 

void via_keyb_update_mouse_pos(uint8 x, uint8 y)
{   
    if (!mouse_enable) return;
    
     if (sizeleft_fifo()>=3) {
        push_fifo(0x00);
        push_fifo(x);
        push_fifo(y);
     }
}

void via_keyb_power_off(void) 
{
        push_fifo(0x80);
        push_fifo(0xFB);
}

void via_keyb_update_mouse_button(uint8 value)
{
     // nothing here... mouse button is a key in lisa protocol
}

// cop can send reset codes
// 0x80 xx
// where xx:
// FF keyb fail
// FD keyb unplugged
// FC clock timer interrupt
// fb soft power pressed
// Ey clock data follows
//          80 Ey qq qh hm ms st 
// 00 - df key id (01)


// normal code is
// drrr nnnn : where
//      d : 0=up 1=down
//   rrr=row nnnn=column


// commands sent to cop
// 0000 0000 turn io port on
// 0000 0001 turn io off
// 0000 0010 read clock
// 0001 nnnn write nnnn to clock
// 0010 spmm set clock mode
// 0011 nnnn write nnnn to low indicator
// 0100 nnnn write nnnn to high indicator
// 0101 nnnn set high nibble of NMI
// 0110 nnnn set low nibble of NMI
// 0111 nnnn mouse update interval
// 1xxx xxxx Null command 

#include <time.h>

void cop_reset_callback(int param);

void send_COP(uint8 code)
{
     idle_time delta;
     IDLE_INIT_FUNC("send_COP()");
     IDLE_DEBUG("called code %d",code);
     switch (code & 0xF0) {
            case 0x00 :
                 {
                      switch (code) {
                            // turn port on
                            case 0x00 :
                                 IDLE_TRACE("COP: Sent 00");
//                                 clean_fifo();
//								 IDLE_TRACE("KEYB RESET");
//								 delta=double2time(0.001);
//								 idle_timer_create(cop_reset_callback,0,COP_KEYB_RESET,delta);       
                                 return;
                            // turn port off
                            case 0x01 :
                                 IDLE_TRACE("COP: Sent 01");
                                 return;
                            // read clock
                            // {0x80,0xEy,0xmm,0xdd,0xhh,0xmm,0xss}
                            case 0x02 : { 
                                 static time_t first_time;
                                 time_t t;
                                 idle_time id=idle_get_time();
                                 static int first=1;
                                 struct tm * ts;
                                 int d1,d2,d3;
                                 int h1,h2;
                                 int m1,m2;
                                 int s1,s2;
                                 if (first) {
                                     t=first_time = time (NULL);
                                     first=0;
                                 }
                                 else {
                                      t=first_time+id.seconds;
                                 }
                                 
                                 ts=localtime (&t);
                                 IDLE_TRACE("COP: clock command time=%u",t);
                                      push_fifo(0x80);
                                      push_fifo(0xE9);
                                      d1=(ts->tm_yday)/100;
                                      d2=((ts->tm_yday)/10)%10;
                                      d3=(ts->tm_yday)%10;
                                      h1=(ts->tm_hour)/10;
                                      h2=(ts->tm_hour)%10;
                                      m1=(ts->tm_min)/10;
                                      m2=(ts->tm_min)%10;
                                      s1=(ts->tm_sec)/10;
                                      s2=(ts->tm_sec)%10;                                      
                                      push_fifo((d1<<4)|d2);
                                      push_fifo((d3<<4)|h1);
                                      push_fifo((h2<<4)|m1);
                                      push_fifo((m2<<4)|s1);
                                      push_fifo((s2<<4)|(id.subseconds/(TIMER_FREQ/10)));
                                 return; 
                                 } 
                      default:
                              return;
                      }
                  }
            // mouse delay
            case 0x70:
                 IDLE_TRACE("Setting mouse freq to %d",code&0x0F);
                 mouse_enable=code&0x0F;
                 return;
            // write nnn to clock     
            case 0x10 :
                    IDLE_WARN("COP: Sent x%02x",code);
                 break;
            // set clock mode
            case 0x20 :
                 if ((code&0x40)==0) {
                    IDLE_TRACE("System halt");
                    exit(0);
                 } 
                 break;
            // write low indicator
            case 0x30 :
            // write high indicator
            case 0x40 :
            // set nmi high
            case 0x50 :
            // set nmi low
            case 0x60 :
            default:
                    IDLE_WARN("COP: Sent x%02x",code);
                    return;
            }     
        
     
}



void via_set_FDIR(void)
{
     via_keyb_irb|=0x10;
     via_set_input_b(KEYB_VIA,via_keyb_irb);
}

void via_reset_FDIR(void)
{
     via_keyb_irb&=0xEF;
     via_set_input_b(KEYB_VIA,via_keyb_irb);
}


void via_keyb_access(uint32 address,int* val,int mode,int size)
{
     int offset=((address&0xFFFF)/VIA_INC)&0xF;
     if (mode==IO_WRITE) via_write(KEYB_VIA,offset,*val);
     else *val=via_read(KEYB_VIA,offset);
}

void via_keyb_access16(uint32 address,int* val,int mode,int size)
{
     int offset=((address&0xFFFF)/VIA_INC)&0xF;
     if (mode==IO_WRITE) via_write(KEYB_VIA,offset,(*val&0xFF));
     else *val=via_read(KEYB_VIA,offset);
}

void keyb_irq(int event) {
     IDLE_INIT_FUNC("keyb_irq()");
     if (event==IRQ_SET) {
        IDLE_DEBUG("IRQ 2 fired");
        set_irq_line(KEYBIRQ);
     }
     else
        reset_irq_line(KEYBIRQ);
     
}

void cop_reset1_callback(int param)
 {
     int new_param;
     IDLE_INIT_FUNC("cop_reset1_callback()");
     IDLE_TRACE("called");
     via_set_input_ca1(KEYB_VIA, 0);
     key_can_read=0;
     clean_fifo();
     push_fifo(0x80);
     push_fifo(0xFD);
}
void cop_reset2_callback(int param)
 {
     int new_param;
     IDLE_INIT_FUNC("cop_reset2_callback()");
     IDLE_TRACE("called");
//     via_set_input_ca1(KEYB_VIA, 0);
     key_can_read=1;
     push_fifo(0x80);
     push_fifo(0x2F);
}
void via_keyb_set_output_b(int value) {
     static int first=1;
     static int last_value=0;
     idle_time delta;
     IDLE_INIT_FUNC("via_keyb_set_output_b()");
     if (first) {
          first=0;
          last_value=value;
          return;
     }
     // detect KEYB RESET
     if (((last_value&0x01)==0x00) && ((value&0x01)==0x01)) {
        IDLE_TRACE("KEYB RESET 2");
        delta=double2time(0.0001);
        idle_timer_create(cop_reset2_callback,0,COP_KEYB_RESET,delta);       
     }
     // detect KEYB RESET
     if (((last_value&0x01)==0x01) && ((value&0x01)==0x00)) {
        IDLE_TRACE("KEYB RESET 1");
        delta=double2time(0.0001);
        idle_timer_create(cop_reset1_callback,0,COP_KEYB_RESET,delta);       
     }

     // detect PROFILE RESET
     if (((last_value&0x80)==0x00) && ((value&0x80)==0x80)) {
        IDLE_TRACE("PROFILE RESET");
        via_reset_PROFILE(0);
     }
     last_value=value;    
     if ((value & 0x10) == 0x00)
        IDLE_TRACE("!!! Clearing FDIR by hand...");
}

void cop_timer_callback(int param) {
     idle_time delta;
     int new_param;
     IDLE_INIT_FUNC("cop_timer_callback()");
     if (param==0) {
     // begin the 20us COPS_READY Phase
        via_keyb_irb&=0xBF;
        via_set_input_b(KEYB_VIA,via_keyb_irb);
        delta=double2time(0.00002);
        new_param=1;
     }
     else {
          uint8 command;
          uint8 data;
     // can now control if a command was sent
     // no handshake to COP
        command=via_get_output_a(KEYB_VIA);
        // can now write data from queue
        if (key_can_read && (pop_fifo(&data)==0))
            via_send_COP2VIA(data);
        else
            if ((command&0x80)==0) send_COP(command);
     // we are now in the COPS not ready phase
        via_keyb_irb|=0x40;
        via_set_input_b(KEYB_VIA,via_keyb_irb);
        delta=double2time(0.0002);
        new_param=0;
     }
     idle_timer_create(cop_timer_callback,new_param,COP_KEYB,delta);       
}

void init_via_keyb_io(void)
{
     idle_time delta;
     int i,j,k;
     IDLE_INIT_FUNC("init_via_keyb_io()");
     for (i=0xDC00;i<0xE000;i+=2) 
     {
             registerIoFuncAdr(i+1,&via_keyb_access);
             registerIoFuncAdr(i,&via_keyb_access16);
     }
     via_init(KEYB_VIA,VIA_KEYB_T1,VIA_KEYB_T2,keyb_irq);
     via_set_output_callback(KEYB_VIA,NULL,via_keyb_set_output_b);
     via_set_clock(KEYB_VIA,500000);
     via_set_input_a(KEYB_VIA,0x00);
     via_set_input_b(KEYB_VIA,0x00);
    via_set_input_ca1(KEYB_VIA, 0);


     delta=double2time(0.002);	
     idle_timer_remove(COP_KEYB);
     IDLE_DEBUG("create timer COP");
     idle_timer_create(cop_timer_callback,0,COP_KEYB,delta);       
}
