/*
  Name: 
  Copyright: 
  Author: 
  Date: 29/03/07 14:29
  
  Description: parallel port via and profile emulation
  
  many details about profile protocol are documented in the idefile project
  
       IDEfile is copyright (c) by Dr. Patrick Schäfer, 2004
       http://john.ccac.rwth-aachen.de:8000/patrick/idefile.htm
*/


#include <stdio.h>
#include "../config.h"
#include <errno.h>

#include "../cpu/m68k.h"

// to register io functions
#include "../mmu/mmu.h"
#include "../time/timer.h"
#include "../time/idle_timer_tags.h"
#include "via.h"
#include "../irq/irq.h"

// the mapping address of this VIA
#define VIABASE 0xD901
// the increment value
#define VIA_INC 8

// PA0-7 = Data Bus 
// CA1=  BSY
// CA2= -PSTRB
// PB0= OCD open cable detect
// PB1= BSY
// PB3= RW
// PB4= CMD
// PB5= 
// PB6= -PARITY
// PB7= (was Baud rate control spec 81) in fact : WCNT write contrast (set by HDD PA)

#define BUSY           0x02
#define HDD_VIA_RW     0x08
#define PCMD           0x10
#define WCNT           0x80

uint8 contrast=0;
uint8 via_hdd_ira=0xFF;
uint8 via_hdd_irb=0xFE; // DF


FILE *profile_file;
static int profile_type;

// enum {READ,WRITE} profile_mode;

uint8 profile_buffer[532];

int profile_on=1;
uint8 pa_bus=0xFF;
uint8 pb_val=0xFF;

int hdd_command_state=0;
uint8 hdd_command[6];


typedef struct _profile_auto_state profile_auto_state;

// the events
typedef enum
{
        TIMEOUT1S,
        PCMDLOW,
        PCMDHIGH,
        PA_SET,
        TIMER,
        END_SEQUENCE
} profile_event_t;

// a disk automanon state description
struct _profile_auto_state {
    char *name;
	void (*func_state)(void); // what to do when getting here
	int (*func_events)(profile_event_t);     // handle events functions
};

static int get_num_state(char *);
static void profile_auto_update(profile_event_t event);
static int send_pa_set_event(int val);
int read_profile_stream(int dummy);
int write_profile_stream(int val);
int read_profile_index=0;
int write_profile_index=0;

void profile_read(int block) {
int ret;
char buf[532];
IDLE_INIT_FUNC("profile_read()");
IDLE_TRACE("reading block %x",block);
ret=fseek(profile_file,block*532,SEEK_SET);
IDLE_TRACE("reading block x%x fseek res",ret);
errno=0;
ret=fread(buf,1,532,profile_file);
IDLE_TRACE("reading block x%x byte read errno=%d",ret,errno);
memcpy(profile_buffer,buf,532);
fflush(profile_file);
}

void profile_write(int block) {
int ret;
char buf[532];
IDLE_INIT_FUNC("profile_write()");
IDLE_TRACE("writing block %x",block);
memcpy(buf,profile_buffer,532);
ret=fseek(profile_file,block*532,SEEK_SET);
ret=fwrite(buf,532,1,profile_file);
fflush(profile_file);
}

// the state functions 
void state_idle(void) {
    IDLE_INIT_FUNC("state_idle()");
    IDLE_DEBUG("called");
    via_set_input_ca1(HDD_VIA,1);
    via_hdd_irb|=BUSY;
    via_set_input_b(HDD_VIA,via_hdd_irb);
}

void state_command1(void) {
    IDLE_INIT_FUNC("state_command1()");
    IDLE_DEBUG("called");
    via_set_input_a(HDD_VIA,0x01);
    via_set_input_ca1(HDD_VIA,0);
    via_hdd_irb&=(~BUSY)&0xFF;
    via_set_input_b(HDD_VIA,via_hdd_irb);
}

void state_command2(void) {
    idle_time delta;
    IDLE_INIT_FUNC("state_command2()");
    IDLE_DEBUG("called");
    // create timer
    delta=double2time(0.0001);	
    idle_timer_remove(PROFILE_TIMOUT2);
    idle_timer_create((void(*)(int))profile_auto_update,(int)TIMER,PROFILE_TIMOUT2,delta);       
}

void state_command3(void) {
    IDLE_INIT_FUNC("state_command3()");
    IDLE_DEBUG("called");
    via_set_input_ca1(HDD_VIA,1);
    via_hdd_irb|=BUSY;
    via_set_input_b(HDD_VIA,via_hdd_irb);
    hdd_command_state=0;
}


void state_read(void) {
    uint32 block=(hdd_command[1]<<16)|(hdd_command[2]<<8)|(hdd_command[3]);
    IDLE_INIT_FUNC("state_read()");
    IDLE_DEBUG("called");
    // 02 to bus
    via_set_input_a(HDD_VIA,0x02);
    //  /PBSY=0
    via_set_input_ca1(HDD_VIA,0);
    via_hdd_irb&=(~BUSY)&0xFF;
    via_set_input_b(HDD_VIA,via_hdd_irb);
    read_profile_index=0;
    if (block<((profile_type==0)?0x2600:0x4C00))
       profile_read(block);
}

void state_read2(void) {
    IDLE_INIT_FUNC("state_read2()");
    IDLE_DEBUG("called");
    via_set_input_ca1(HDD_VIA,1);
    via_hdd_irb|=BUSY;
    via_set_input_b(HDD_VIA,via_hdd_irb);
}

void state_write(void) {
    IDLE_INIT_FUNC("state_write()");
    IDLE_DEBUG("called");
    // 03 or 04 to bus
    if (hdd_command[0]==1)
        via_set_input_a(HDD_VIA,0x03);
    else
        via_set_input_a(HDD_VIA,0x04);
    //  /PBSY=0
    via_set_input_ca1(HDD_VIA,0);
    via_hdd_irb&=(~BUSY)&0xFF;
    via_set_input_b(HDD_VIA,via_hdd_irb);
    write_profile_index=0;
}

void state_write2(void) {
    IDLE_INIT_FUNC("state_write2()");
    IDLE_DEBUG("called");
    via_set_input_ca1(HDD_VIA,1);
    via_hdd_irb|=BUSY;
    via_set_input_b(HDD_VIA,via_hdd_irb);
}

void state_write3(void) {
    IDLE_INIT_FUNC("state_write3()");
    IDLE_DEBUG("called");
    // 06 to bus
    via_set_input_a(HDD_VIA,0x06);
    //  /PBSY=0
    via_set_input_ca1(HDD_VIA,0);
    via_hdd_irb&=(~BUSY)&0xFF;
    via_set_input_b(HDD_VIA,via_hdd_irb);
    read_profile_index=0;
}

void state_write5(void) {
    uint32 block=(hdd_command[1]<<16)|(hdd_command[2]<<8)|(hdd_command[3]);
    IDLE_INIT_FUNC("state_write5()");
    IDLE_DEBUG("called");
    via_set_input_ca1(HDD_VIA,1);
    via_hdd_irb|=BUSY;
    via_set_input_b(HDD_VIA,via_hdd_irb);

    if (block<((profile_type==0)?0x2600:0x4C00))
       profile_write(block);
}

// the events functions
int event_idle(profile_event_t event) {
    IDLE_INIT_FUNC("event_idle()");
    switch (event) {
           case PCMDLOW:
                   return get_num_state("command_1"); // go to command 1 state
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}
int event_command1(profile_event_t event) {
    IDLE_INIT_FUNC("event_command1()");
    switch (event) {
           case PCMDHIGH:
                   IDLE_DEBUG("pa_bus==0x%x",pa_bus);
                   if (pa_bus==0x55) {
                      via_set_stream_a_callback(HDD_VIA,send_pa_set_event);
                      return get_num_state("command_2");
                   } 
                   else
                      return get_num_state("idle");          
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_command2(profile_event_t event) {
    IDLE_INIT_FUNC("event_command2()");
    switch (event) {
           case TIMER:
                   return get_num_state("command_3");                   
           case PCMDLOW:
                   return get_num_state("idle");                   
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_command3(profile_event_t event) {
    IDLE_INIT_FUNC("event_command3()");
    switch (event) {
           case PA_SET:
                   if ((pb_val&HDD_VIA_RW)!=0) {
                      IDLE_DEBUG("write not in write mode ignore");
                      return -1;
                   }
                   if (hdd_command_state==6) {  
                      IDLE_WARN("command buffer overflow");                                                              
                      return -1;
                      }
                   // get a command value
                   hdd_command[hdd_command_state]=pa_bus;
                   IDLE_DEBUG("command[%d]=%d",hdd_command_state,pa_bus);
                   hdd_command_state++;
                   if (hdd_command_state==6) {
                      IDLE_DEBUG("command complete");
                   }
                   return -1;                   
           case PCMDLOW:
               via_set_stream_a_callback(HDD_VIA,NULL);
                // read command found
                if (hdd_command[0]==0) {
                   return get_num_state("read");                   
                }
                if (hdd_command[0]==1) {
                   return get_num_state("write");                   
                }
                if (hdd_command[0]==2) {
                   return get_num_state("write");                   
                }
                return get_num_state("idle");                   
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}


int event_read(profile_event_t event) {
    IDLE_INIT_FUNC("event_read()");
    switch (event) {
           case PCMDHIGH:
                   IDLE_DEBUG("pa_bus==0x%x",pa_bus);
                   if (pa_bus==0x55) {
                      via_set_stream_a_callback(HDD_VIA,read_profile_stream);
                      return get_num_state("read_2");
                   } 
                   else
                      return get_num_state("idle");          
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_read2(profile_event_t event) {
    IDLE_INIT_FUNC("event_read2()");
    switch (event) {
           case TIMEOUT1S:
           case END_SEQUENCE:
                   via_set_stream_a_callback(HDD_VIA,NULL);               
                   return get_num_state("idle"); // go to command 1 state
           case PCMDLOW:
                   via_set_stream_a_callback(HDD_VIA,NULL);               
                   return get_num_state("command_1"); // go to command 1 state
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_write(profile_event_t event) {
    IDLE_INIT_FUNC("event_write()");
    switch (event) {
           case PCMDHIGH:
                   IDLE_DEBUG("pa_bus==0x%x",pa_bus);
                   if (pa_bus==0x55) {
                      via_set_stream_a_callback(HDD_VIA,write_profile_stream);
                      return get_num_state("write_2");
                   } 
                   else
                      return get_num_state("idle");          
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_write2(profile_event_t event) {
    IDLE_INIT_FUNC("event_write2()");
    switch (event) {
           case PCMDLOW:
                   via_set_stream_a_callback(HDD_VIA,NULL);               
                   return get_num_state("write_3"); // go to command 1 state
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_write3(profile_event_t event) {
    IDLE_INIT_FUNC("event_write3()");
    switch (event) {
           case PCMDHIGH:
                   IDLE_DEBUG("pa_bus==0x%x",pa_bus);
                   if (pa_bus==0x55) {
                      via_set_stream_a_callback(HDD_VIA,read_profile_stream);
                      return get_num_state("write_5");
                   } 
                   else
                      return get_num_state("idle");          
           case TIMEOUT1S:
                   return get_num_state("idle");                                   
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

int event_write5(profile_event_t event) {
    IDLE_INIT_FUNC("event_write5()");
    switch (event) {
           case TIMEOUT1S:
           case END_SEQUENCE:
                   via_set_stream_a_callback(HDD_VIA,NULL);               
                   return get_num_state("idle"); // go to command 1 state
           case PCMDLOW:
                   via_set_stream_a_callback(HDD_VIA,NULL);               
                   return get_num_state("command_1"); // go to command 1 state
           default:
                   IDLE_DEBUG("Unexpected event %d",(int)event);
                   return -1;
    }
}

// the machine state
profile_auto_state profile_auto[]= {
{"idle",             state_idle,                event_idle},          
{"command_1",        state_command1,            event_command1},  
{"command_2",        state_command2,            event_command2},          
{"command_3",        state_command3,            event_command3},          
{"read",             state_read,                event_read},          
{"read_2",           state_read2,               event_read2},          
{"write",            state_write,               event_write},         
{"write_2",          state_write2,              event_write2},         
{"write_3",          state_write3,              event_write3},         
{"write_5",          state_write5,              event_write5}          
};

// helper function for state names
int get_num_state(char * name) {
    int i;
    IDLE_INIT_FUNC("get_num_state()");    
    for (i=0;i<(sizeof(profile_auto)/sizeof(profile_auto_state));i++) {
        if (strcmp(profile_auto[i].name,name)==0) return i;
    }
    IDLE_WARN("Cannot find state %s",name);
    return -1;
}

static int profile_state=0;


void profile_auto_update(profile_event_t event) {
    idle_time delta;
    int ret,next;
    IDLE_INIT_FUNC("profile_auto_update()");
    IDLE_DEBUG("called phase=%d %s",profile_state,profile_auto[profile_state].name);
    // calls func
    if (profile_auto[profile_state].func_events!=NULL) {
          next=(*profile_auto[profile_state].func_events)(event);
          if (next!=-1) {
             profile_state=next;
             // if we changed state : call state func (if exists)
             if (profile_auto[profile_state].func_state!=NULL)
                (*profile_auto[profile_state].func_state)();
          }
          
    }
    // rearm timeout
    delta=double2time(1.0);	
    idle_timer_remove(PROFILE_TIMOUT);
    idle_timer_create((void(*)(int))profile_auto_update,(int)TIMEOUT1S,PROFILE_TIMOUT,delta);       
}

// the via part
void via_hdd_access(uint32 address,int* val,int mode,int size)
{
     int offset=(address/VIA_INC)&0xF;
     if (mode==IO_WRITE) via_write(HDD_VIA,offset,*val);
     else *val=via_read(HDD_VIA,offset);
}

void via_hdd_access16(uint32 address,int* val,int mode,int size)
{
     int offset=(address/VIA_INC)&0xF;
     if (mode==IO_WRITE) via_write(HDD_VIA,offset,(*val)&0xFF);
     else *val=via_read(HDD_VIA,offset);
}

void via_set_DIAG(void)
{
    IDLE_INIT_FUNC("via_set_DIAG()");
    if ((via_hdd_irb&0x40)==0) {
        IDLE_DEBUG("DIAG is set");
    }
     via_hdd_irb|=0x40;
     via_set_input_b(HDD_VIA,via_hdd_irb);
}

void via_reset_DIAG(void)
{
    IDLE_INIT_FUNC("via_reset_DIAG()");
    if ((via_hdd_irb&0x40)!=0) {
        IDLE_DEBUG("DIAG is removed");
    }
     via_hdd_irb&=0xBF;
     via_set_input_b(HDD_VIA,via_hdd_irb);
}

void hdd_irq(int event) {
     IDLE_INIT_FUNC("hdd_irq()");
     if (event==IRQ_SET) {
        IDLE_DEBUG("IRQ 1 fired");
        // m68k_set_irq(M68K_IRQ_1);
        set_irq_line(HDDIRQ);
     }
     else
        reset_irq_line(HDDIRQ);
     
}




#define PROFILE_STR_LEN (4+13+3+2+3+2+1+1+1+4)
char profile_str[PROFILE_STR_LEN]={
     0x00,0x00,0x00,0x00,
     'P','R','O','F','I','L','E',' ',' ',' ',' ',' ',' ',
     0x00,0x00,0x00,
     0x03,0x98,
     0x00,0x26,0x00,
     0x02,0x14,
     0x20,
     0x00,
     0x00,
     0xff, 0xff, 0xff, 0x00 }; 

char widget_str[PROFILE_STR_LEN]={
     0x00,0x00,0x00,0x00,
     'W','I','D','G','E','T',' ',' ',' ',' ',' ',' ',' ',
     0x00,0x00,0x00,
     0x03,0x98,
     0x00,0x4C,0x00,
     0x02,0x14,
     0x20,
     0x00,
     0x00,
     0xff, 0xff, 0xff, 0x00 }; 


int read_profile_stream(int dummy) {
    int val;
    IDLE_INIT_FUNC("read_profile_stream()");
    uint32 block=((hdd_command[1] & 0xFF)<<16)|(hdd_command[2]<<8)|(hdd_command[3]);
    if (block==0xFFFFFF) {
        if (profile_type==0)
                val=profile_str[read_profile_index];
        else
                val=widget_str[read_profile_index];
        
        // discards the stream at end
        if (read_profile_index<PROFILE_STR_LEN) {
        read_profile_index++;
        }


    }
    else {
    // in status 
    if (read_profile_index<4) {
       val=0;
    }
    // in data
    else if (read_profile_index<512+4) {
       val=profile_buffer[read_profile_index-4];
    }
    // in tags
    else {
       val=profile_buffer[read_profile_index-4];
    }
    read_profile_index++;
    // discards the stream at end
    if (  (read_profile_index>535) ||
          ((read_profile_index>4) && (hdd_command[0]!=0))  )
    {
       IDLE_DEBUG("Ending stream");
       via_set_stream_a_callback(HDD_VIA,NULL);
       profile_auto_update(END_SEQUENCE);
    }
    }
    IDLE_DEBUG("read[%x]=%x",read_profile_index,val);   
    return val;
}



int write_profile_stream(int val) {
    IDLE_INIT_FUNC("write_profile_stream()");
    // in data
    if (write_profile_index<512) {
       IDLE_DEBUG("in data[%d]=%d",write_profile_index,val);
       profile_buffer[write_profile_index]=val;
    }
    // in tags
    else {
       IDLE_DEBUG("in tags[%d]=%d",write_profile_index-512,val);
       profile_buffer[write_profile_index]=val;
    }
    write_profile_index++;
    
    // discards the stream at end
    // next will follow acknoledge phase
    if (write_profile_index>531) {
       IDLE_DEBUG("Ending stream");
       via_set_stream_a_callback(HDD_VIA,NULL);
    }
    
    return val;
}





void via_reset_PROFILE(int dummy) {
     IDLE_INIT_FUNC("via_reset_PROFILE()");
     IDLE_TRACE("PROFILE RESET");
     idle_timer_remove(PROFILE_TIMOUT);
     via_set_input_ca1(HDD_VIA,1);
     via_hdd_irb|=0x02;
     via_set_input_b(HDD_VIA,via_hdd_irb);
     via_set_stream_a_callback(HDD_VIA,NULL);
}

void create_profile_timeout(void) {
     idle_time delta;
     IDLE_INIT_FUNC("create_profile_timeout()");
     idle_timer_remove(PROFILE_TIMOUT);
     IDLE_DEBUG("create timer PROFILE");
     delta=double2time(1.0);	
     idle_timer_create(via_reset_PROFILE,0,PROFILE_TIMOUT,delta);       
}

int send_pa_set_event(int val) {
     pa_bus=val; // fixme...
     profile_auto_update(PA_SET);
     return val;
}

void via_hdd_set_output_a(int value) {
     IDLE_INIT_FUNC("via_hdd_set_output_a()");
     IDLE_DEBUG("called val=x%x",value);
     
     
     if (contrast) {
        IDLE_TRACE("Setting contrast to %x",value);
        goto end;
     }     

     pa_bus=value;

end:
    return;
}

void via_hdd_set_output_b(int value) {
     static uint8 last=0xFF;
     IDLE_INIT_FUNC("via_hdd_set_output_b()");
     IDLE_DEBUG("called val=0x%x",value);
     
     pb_val=value;
     
     // if write contrast
     if (((value & WCNT) == 0) && ((last&WCNT)!=0)) {
        IDLE_TRACE("Write Contrast on");
        contrast=1;
     }
     if (((value & WCNT) != 0) && ((last&WCNT)==0)) {
        IDLE_TRACE("Write Contrast off");
        contrast=0;
     }
     
     if (profile_on==0) return;
     
     // if PCMD is set low
     if (((value&PCMD)==0) && ((last&PCMD)!=0)) {
        IDLE_DEBUG("PCMD set LOW");
        profile_auto_update(PCMDLOW);
     }
     
     // if PCMD is set High
     if (((value&PCMD)!=0) && ((last&PCMD)==0)) {
        IDLE_DEBUG("PCMD set HIGH");
        profile_auto_update(PCMDHIGH);
     }      
end:     
     last=value;
}

void via_turn_on_PROFILE(void) {
     profile_on=1;
}

void via_turn_off_PROFILE(void) {
     profile_on=0;
}

int check_profile_image(void) {
    int ret;
    long size;
    IDLE_INIT_FUNC("check_profile_image()");
    profile_file=fopen("profile.raw","rb");
    if (profile_file==NULL) return -1;
    else {
         size=fseek(profile_file,0,SEEK_END);
         size=ftell(profile_file);
         IDLE_TRACE("size is %d",size);
         switch (size/532) {case 0x2600:
                                        profile_type=ret=0;
                                        IDLE_TRACE("Found 5Mb profile image");
                                        break; 
                            case 0x4C00:
                                        profile_type=ret=1;
                                        IDLE_TRACE("Found 10Mb profile image");
                                        break; 
                            default :
                                        ret=-1;
                                        IDLE_TRACE("Found bad profile image !");
                                        break; 
                                        }
         
         ret=0;
    }
    fclose(profile_file);
    return ret;
}

void init_via_hdd_io(int profile_type_a)
{
     IDLE_INIT_FUNC("init_via_hdd_io()");
     int i,j,k;

     if (profile_type_a!=-1)     
          profile_type=profile_type_a;
     
     for (i=0xD800;i<0xDC00;i+=2)
     {
             registerIoFuncAdr(i+1,&via_hdd_access);
             registerIoFuncAdr(i,&via_hdd_access16);
     }
     via_init(HDD_VIA,VIA_HDD_T1,VIA_HDD_T2,hdd_irq);
     via_set_output_callback(HDD_VIA,via_hdd_set_output_a,via_hdd_set_output_b);

     via_set_clock(HDD_VIA,500000);
     via_set_input_a(HDD_VIA,via_hdd_ira);
     via_set_input_b(HDD_VIA,via_hdd_irb);
     
          
     if (profile_type_a!=-1) {
        char buf[532];
        int ret;
        memset(buf,0,532);
        profile_file=fopen("profile.raw","wb");
        if (profile_file==NULL) {
           IDLE_TRACE("cannot create profile...");
           exit(1);
        }
        for (i=0;i<((profile_type==0)?0x2600:0x4C00);i++) {
            ret=fwrite(buf,532,1,profile_file);
        }
        fclose(profile_file);
     }     
     
     profile_file=fopen("profile.raw","rb+");
     rewind(profile_file);
     if (profile_file==NULL) {
        IDLE_TRACE("cannot create profile...");
        exit(1);
     }
     // profile_mode=WRITE;
}
