#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"

// to register io functions
#include "../mmu/mmu.h"
#ifdef _CAST_
#include "../cpu/68000.h"
#include "../cpu/op68k.h"
#else
#include "../cpu/m68k.h"
#endif
#include "../io/via.h"


#include "disk_image.h"

#include "../time/timer.h"
#include "../time/idle_timer_tags.h"
#include "../irq/irq.h"

/*
real mapping is
DISKMEM .EQU $00FCC001 ;base address of shared memory
CMD .EQU 2 ;offset for command byte
DRV .EQU CMD+2 ;offset for drive #
SIDE .EQU DRV+2 ;side #
SCTR .EQU SIDE+2 ;sector #
TRAK .EQU SCTR+2 ;track #
SPEED .EQU TRAK+2 ;motor speed control
CNFRM .EQU SPEED+2 ;confirm for format cmd
STAT .EQU CNFRM+2 ;error status
INTLV .EQU STAT+2 ;interleave factor CHG022
TYPE .EQU INTLV+2 ;drive type id CHG009
STST .EQU TYPE+2 ;self-test result CHG022
ROMV .EQU $30 ;ROM version #
RTRYCNT .EQU $58 ;retry count
INTSTAT .EQU $5E ;interrupt status
CHKCNT .EQU $BA ;data checksum error count
CHKCNT2 .EQU $C4 ;address checksum error count
DSKBUFF .EQU $3E8 ;start of disk buffer
DSKDATA .EQU DSKBUFF+24 ;first 12 bytes are header
DISKROM .EQU $FCC031 ;absoulte address for disk ROM id

real commands are (from Boot rom listing)

READS .EQU 0 ;read sector w/checksum
WRT .EQU 1
UNCLAMP .EQU 2 ;unclamp diskette
FMT .EQU 3
VFY .EQU 4 ;verify disk
CLAMP .EQU 9 ;clamp disk
OK .EQU $FF ;confirmation for format
SEEK .EQU $83 ;seek cmd
EXRW .EQU $81 ;execute cmd
CLRSTAT .EQU $85 ;clear status cmd
ENBLINT .EQU $86 ;enable intrpt
DSABLINT .EQU $87 ;disable intrpt
SLEEP .EQU $88 ;loop in RAM cmd
DIE .EQU $89 ;loop in ROM cmd


*/

static int disk_present=0;

static int s_revision=0xA8;

static int drive;
static int side;
static int sector;
static int track;
static int error;

static int command_time;
uint8 command;
uint8 gobyte;

uint8 floppy_shared_tab[0x400];

// a disk automanon state description
struct _floppy_auto_state {
    char *name;
	int (*func)(void);
	int  default_next;
	double wait;
};

typedef struct _floppy_auto_state floppy_auto_state;

void E_1518(void);

// states defines
int state_init(void);
int state_idle_fdd(void);
int state_gobyte(void);
int state_command(void);

int state_80(void);
int state_81(void);
int state_84(void);
int state_84_bis(void);
int state_85(void);
int state_86(void);
int state_87(void);
int state_88(void);

int state_81_00(void);
int state_81_01(void);
int state_81_02(void);
int state_81_09(void);
int state_81_00_bis(void);
int state_81_01_bis(void);
int state_81_02_bis(void);

// the machine state
floppy_auto_state floppy_auto[]= {
{"init",                state_init,       1,  0.500}, // 0
{"idle",                state_idle_fdd,             2,  0.0001}, // 1
{"check_gobyte",        state_gobyte,     1,  0.0001}, // 2
{"got_command",         state_command,    1,  0.00001}, // 3
{"got_80",              state_80,         1,  0.0001}, // 4
{"got_81",              state_81,         1,  0.0001}, // 5
{"got_84",              state_84,         11, 0.0001}, // 6
{"got_85",              state_85,         1,  0.00001}, // 7
{"got_86",              state_86,         1,  0.00001}, // 8
{"got_87",              state_87,         1,  0.00001}, // 9
{"got_88",              state_88,         1,  0.0001}, // 10
{"got_84_bis",          state_84_bis,     1,  0.0001}, // 11
{"void",                NULL,             0,  1.0},   // 12
{"void",                NULL,             0,  1.0},   // 13
{"void",                NULL,             0,  1.0},   // 14
{"void",                NULL,             0,  1.0},   // 15
{"got_81_00",           state_81_00,      17, 0.00001}, // 16
{"got_81_00_bis",       state_81_00_bis,  1,  0.00001}, // 17
{"got_81_01",           state_81_01,      19, 0.00002}, // 18
{"got_81_01_bis",       state_81_01_bis,  1,  0.00002}, // 19
{"got_81_02",           state_81_02,      21, 0.001}, // 20
{"got_81_02_bis",       state_81_02_bis,  1,  0.01}, // 21
{"got_81_09",           state_81_09,      1, 0.01}, // 22
{"void",                NULL,             0,  1.0}    // ..
};

void floppy_callback(int state) {
    idle_time delta;
    int ret,next;
    IDLE_INIT_FUNC("floppy_callback()");
    IDLE_DEBUG("called phase=%d %s",state,floppy_auto[state].name);
    // calls func
    if (floppy_auto[state].func!=NULL) {
          ret=(*floppy_auto[state].func)();
          if (ret==-1)
              next=floppy_auto[state].default_next;
          else
              next=ret;
    }
    else
          next=floppy_auto[state].default_next;
    
    delta=double2time(floppy_auto[state].wait);	
    idle_timer_remove(FLOPPY_REQ);
    idle_timer_create(floppy_callback,next,FLOPPY_REQ,delta);       
}

// 1429
int state_init(void) {
    IDLE_INIT_FUNC("state_init()");
    IDLE_DEBUG("called");
    via_set_DIAG();
    via_reset_FDIR();
    return -1;
}

int state_idle_fdd(void) {
    IDLE_INIT_FUNC("state_idle_fdd()");
    IDLE_DEBUG("called");
    if (check_inserted(1)) {
       IDLE_TRACE("Disk inserted event");
       disk_present=1;
       floppy_shared_tab[0x20]=0xff; // disk is in place
       floppy_shared_tab[0x002f]|=0x10;
       via_set_DIAG(); 
       E_1518();
       return -1;           
    } 
    if (check_inserted(0)) {
       IDLE_TRACE("Disk inserted event");
       disk_present=1;
       floppy_shared_tab[0x20]=0xff; // disk is in place
       floppy_shared_tab[0x002f]|=0x01;
       via_set_DIAG(); 
       E_1518();
       return -1;           
    } 

    return -1;
}

// 1496
int state_gobyte(void) {
    IDLE_INIT_FUNC("state_gobyte()");
    IDLE_DEBUG("called");
//    via_set_DIAG();
    if ((gobyte&0x80)==0x80) {
        via_set_DIAG();
       IDLE_DEBUG("Got command %x (%x)",gobyte,command);
       return 3; // go to got_command
    }
    return -1;
}

int memo_gobyte;
int memo_command;
int memo_drive;
int memo_track;
int memo_sector;
int memo_side;

// 15B2
int state_command(void) {
    int prev_gobyte;
    IDLE_INIT_FUNC("state_command()");
    IDLE_DEBUG("called");
    if ((gobyte&0x80)!=0x80) {
       IDLE_TRACE("Command disapeared...");
       return -1;
    }

    memo_gobyte=gobyte;
    memo_command=command;
    memo_drive=drive;
    memo_side=side;
    memo_track=track;
    memo_sector=sector;
    
    switch (memo_gobyte) {
           case 0x80 : return 4;
           case 0x81 :
           case 0x83 : // seek
                 gobyte=0;
                 return 5;
           case 0x84 : return 6;
           case 0x85 : return 7;
           case 0x86 : return 8;
           case 0x87 : return 9;
           case 0x88 : return 10;
    }
    return -1;
}

// 15F8
int state_80(void) {
    IDLE_INIT_FUNC("state_80()");
    IDLE_TRACE("Handshake");
    floppy_shared_tab[0x0008]=0;
    gobyte=0;

    via_reset_DIAG();
    
    return -1;
}

// 1618
int state_81(void) {
    IDLE_INIT_FUNC("state_81()");
    IDLE_TRACE("called command=0x%x",memo_command);
    switch (memo_command) {
           case 0x00 : return 16; 
           case 0x01 : return 18;
           case 0x02 : return 20;
           case 0x09 : return 22;
    }
    return -1;
}

int state_84(void) {
    IDLE_INIT_FUNC("state_84()");
    IDLE_TRACE("Call user prog");
    floppy_shared_tab[0x0006]=0;
    gobyte=0;
    return -1;
}

int state_84_bis(void) {
    IDLE_INIT_FUNC("state_84_bis()");
    IDLE_TRACE("Call user prog");
    floppy_shared_tab[0x0006]=0xFF;
    E_1518();   
    return -1;
}


int state_85(void) {
    IDLE_INIT_FUNC("state_85()");
    IDLE_DEBUG("reset int");
    floppy_shared_tab[0x002f]&=(!memo_command)&0xFF;
    floppy_shared_tab[0x0008]=0;
    gobyte=0;
    E_1518();           
    return -1;
}


int state_86(void) {
    IDLE_INIT_FUNC("state_86()");
    IDLE_DEBUG("drive int disable");
    floppy_shared_tab[0x0008]=0;
    floppy_shared_tab[0x002C]|=memo_command;
    gobyte=0;
    E_1518();           
    return -1;
}

int state_87(void) {
    IDLE_INIT_FUNC("state_87()");
    IDLE_DEBUG("called");
    floppy_shared_tab[0x0008]=0;
    floppy_shared_tab[0x002C]&=(!memo_command)&0xFF; 
    gobyte=0;
    E_1518();           
    return -1;
}

int state_88(void) {
    IDLE_INIT_FUNC("state_88()");
    IDLE_TRACE("called");
    gobyte=0;
    return -1;
}


int state_81_00(void) {
    uint8 *data;
    uint8 *tags;
    int datasize;
    int tagsize;
    int ret;
    int i;
    IDLE_INIT_FUNC("state_81_00()");
    IDLE_DEBUG("called");
    if (!disk_present) {
        floppy_shared_tab[0x0008]=0x07;
        IDLE_TRACE("read with no disk");
        return -1;
    }

    IDLE_DEBUG("Read sect%d track%d",sector,track);
    ret=read_sector(((memo_drive==0)?0:1),memo_track,memo_sector,memo_side,
                    &data,&tags,&datasize,&tagsize);
    IDLE_DEBUG("ret=%d",ret);
    if (ret==0)
    {
       floppy_shared_tab[0x0008]=0;
       for (i=0x005B;i<0x0063;i++)
           floppy_shared_tab[i]=0;
                            
       memcpy(&floppy_shared_tab[0x1F4],tags,tagsize);
       memcpy(&floppy_shared_tab[0x1F4+tagsize],data,datasize);
       IDLE_TRACE("FILEID %02x%02x",tags[4],tags[5]);
    }
    else
       floppy_shared_tab[0x0008]=ret;
    
    return -1;
}

int state_81_01(void) {
    int ret;
    int i;
    uint8 wdata[512];
    uint8 wtags[12];
    IDLE_INIT_FUNC("state_81_01()");
    IDLE_DEBUG("called");
    IDLE_DEBUG("Write sect%d track%d",sector,track);
    if (!disk_present) {
        floppy_shared_tab[0x0008]=0x07;
        IDLE_TRACE("write with no disk");
        return -1;
    }

    floppy_shared_tab[0x0008]=0;
    for (i=0x005B;i<0x0063;i++)
       floppy_shared_tab[i]=0;
                            
    memcpy(wdata,&floppy_shared_tab[0x1F4+12],512);
    memcpy(wtags,&floppy_shared_tab[0x1F4],12);
    IDLE_DEBUG("FILEID %02x%02x",wtags[4],wtags[5]);

    ret=write_sector(((memo_drive==0)?0:1),memo_track,memo_sector,memo_side,
                     wdata,wtags);
    IDLE_DEBUG("ret=%d",ret);

    return -1;
}

int state_81_02(void) {
    IDLE_INIT_FUNC("state_81_02()");
    IDLE_TRACE("called UNCLAMP");
    floppy_shared_tab[0x0008]=0;
    via_reset_DIAG();
    return -1;
}

int state_81_00_bis(void) {
    IDLE_INIT_FUNC("state_81_00_bis()");
    IDLE_TRACE("called end READ");
    via_reset_DIAG();
    if (memo_drive!=0)
        floppy_shared_tab[0x002f]|=0x40;
    else
        floppy_shared_tab[0x002f]|=0x04;
    E_1518();           
    gobyte=0;
    return -1;
}

int state_81_01_bis(void) {
    IDLE_INIT_FUNC("state_81_01_bis()");
    IDLE_DEBUG("called end WRITE");
    via_reset_DIAG();
    if (memo_drive!=0)
        floppy_shared_tab[0x002f]|=0x40;
    else
        floppy_shared_tab[0x002f]|=0x04;
    E_1518();         
    gobyte=0;  
    return -1;
}

int state_81_02_bis(void) {
   IDLE_INIT_FUNC("state_81_02_bis()");
    IDLE_TRACE("called end UNCLAMP");
    via_reset_DIAG();
    if (memo_drive!=0)
        floppy_shared_tab[0x002f]|=0x40;
    else
        floppy_shared_tab[0x002f]|=0x04;
    floppy_shared_tab[0x0020]=0x00;
    E_1518();           
    gobyte=0;
    unclamp(drive);
    disk_present=0;
    return -1;
}

int state_81_09(void) {
    IDLE_INIT_FUNC("state_81_09()");
    IDLE_TRACE("called");
    via_reset_DIAG();
    floppy_shared_tab[0x0008]=0;
    if (memo_drive!=0)
        floppy_shared_tab[0x002f]|=0x40;
    else
        floppy_shared_tab[0x002f]|=0x04;
    E_1518();           
    return -1;
}


#define CMD_NONE -1
#define CMD_UNCLAMP 0
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_RESET_INT 3
#define CMD_HANDSHAKE 4
#define CMD_DRIVE_ENABLE 5
#define CMD_CALL         6

static int command_type=CMD_NONE;



void floppy_command(uint32 adr,int* val,int mode,int size)
{
     static int init=0;
     IDLE_INIT_FUNC("floppy_command()");
     // fixme what if byte access?
     if (mode==IO_WRITE)
     {
            IDLE_DEBUG("set command %02x at get_pc=%06x",*val&0xff,get_pc);
            command=*val&0xFF;
     }
     else
     {
         *val=command;
     }
}

void floppy_stst(uint32 adr,int* val,int mode,int size)
{
     static int init=0;
     IDLE_INIT_FUNC("floppy_stst()");
     // fixme what if byte access?
     if (mode==IO_WRITE)
     {
        IDLE_DEBUG("write floppy stst");
     }
     else
     {
         *val=0x00;
     }
}

// this is the end of command routine (mapped in 0x1518 in IO ROM A8)
void E_1518(void) {
    IDLE_INIT_FUNC("E_1518()");
     if (floppy_shared_tab[0x002f]&0x70) floppy_shared_tab[0x002f]|=0x80;
     if (s_revision == 0x40) {
        if (floppy_shared_tab[0x002f]&0x07) floppy_shared_tab[0x002f]|=0x08;
     }
     IDLE_TRACE("New 2F value %x",floppy_shared_tab[0x002f]);
     floppy_shared_tab[0x002E]=floppy_shared_tab[0x002C]&floppy_shared_tab[0x002F];    
     if (floppy_shared_tab[0x002E]!=0) { 
        via_set_FDIR();
        // FDIR is connected via a NAND (open drain) to /IOIR 
        // when FDIR is high => IRQ goes low
        // m68k_set_irq(M68K_IRQ_1);
        set_irq_line(FDIRIRQ);
     }
     else { 
        via_reset_FDIR();
        reset_irq_line(FDIRIRQ);
     }
}

int floppy_tick(void)
{
    IDLE_INIT_FUNC("floppy_tick()");
    return 0;
}



void floppy_gobyte(uint32 adr,int* val,int mode,int size)
{
     uint8 *data;
     uint8 *tags;
     int ret;
     int i;
  
     IDLE_INIT_FUNC("floppy_gobyte()");     
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         IDLE_TRACE("Writing gobyte 0x%x",*val&0xFF);
         gobyte=(*val)&0xFF;
         return;
     } // of mode WRITE
     else
     {
         *val=gobyte;
     }
}


void floppy_drive(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_drive()");
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         drive=(*val)&0xFF;
         IDLE_TRACE("Set drive %x",drive);
     }
     else
     {
         *val=drive;
     }
}


void floppy_side(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_side()");
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         side=(*val)&0xFF;
     }
     else
     {
         *val=side;
     }
}

void floppy_sector(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_sector()");
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         sector=(*val)&0xFF;
     }
     else
     {
         *val=sector;
     }
}

void floppy_track(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_track()");
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         track=(*val)&0xFF;
     }
     else
     {
         *val=track;
     }
}

void floppy_error(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_error()");
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
     if (mode==IO_WRITE)
     {
         error=(*val)&0xFF;
     }
     else
     {
         *val=error;
     }
}

void floppy_shared(uint32 adr,int* val,int mode,int size)
{
     IDLE_INIT_FUNC("floppy_shared()");
     uint32 offset=(adr&0x0fff)>>1;
     if ((size==IO_BYTE) && (adr&1)==0) {
        IDLE_TRACE("Byte access unexpected here");
        return;
     }
//     if (size==IO_WORD) IDLE_WARN("Is Word!");
     if (mode==IO_WRITE)
     {
         if (offset<0x1DF)
            IDLE_DEBUG("write off%x val=%x",offset,*val);
//         if (offset!=0x2f)
         floppy_shared_tab[offset]=(*val)&0xFF;
     }
     else
     {
         *val=floppy_shared_tab[offset];
         if (offset<0x1DF)
            IDLE_DEBUG("read offset %x got %x",offset,*val);
     }
}

extern char* sharedram_box;
void refresh_sharedram_box(void)
{
     char buf[256];
     int addr;
     sharedram_box[0]='\0';

     for (int addr=0;addr<0x400;addr++)
         {
             sprintf(buf,"[%04x] = %02x\n",addr,floppy_shared_tab[addr]);
             strcat(sharedram_box,buf);  
         }                           
}
 
void init_floppy_io(int revision)
{
     int i;

     IDLE_INIT_FUNC("init_floppy_io()");

     registerIoFuncAdr(0xC000 ,&floppy_gobyte);
     registerIoFuncAdr(0xC001 ,&floppy_gobyte);
     registerIoFuncAdr(0xC002 ,&floppy_command);
     registerIoFuncAdr(0xC003 ,&floppy_command);
     registerIoFuncAdr(0xC004 ,&floppy_drive);
     registerIoFuncAdr(0xC005 ,&floppy_drive);
     registerIoFuncAdr(0xC006 ,&floppy_side);
     registerIoFuncAdr(0xC007 ,&floppy_side);
     registerIoFuncAdr(0xC008 ,&floppy_sector);
     registerIoFuncAdr(0xC009 ,&floppy_sector);
     registerIoFuncAdr(0xC00A ,&floppy_track);
     registerIoFuncAdr(0xC00B ,&floppy_track);
     
     for (i=0xC00C;i<0xC801;i+=1)
          registerIoFuncAdr(i ,&floppy_shared);

     for (i=0;i<0x400;i++)     
         floppy_shared_tab[i]=0x00; 

    s_revision=revision;
    IDLE_TRACE("io model is:%x",revision);

    floppy_shared_tab[0x18]=s_revision; // rom version
    
     floppy_shared_tab[0x0A]=0x01; // disk type = sony single sided
     floppy_shared_tab[0x24]=0xff; // disk present ?
     floppy_shared_tab[0x25]=0x02; // disk type = sony single sided

     floppy_shared_tab[0x46]=0xFF; // 
     floppy_shared_tab[0x09]=2; // lisa disk
     floppy_shared_tab[0x5C]=10; // lisa disk
     floppy_shared_tab[0x20]=0x00; // disk is in place
     
         

     registerIoFuncAdr(0xC017 ,&floppy_stst);
     
     // launch the floppy state machine
     idle_timer_remove(FLOPPY_REQ);
     floppy_callback(0);
}
