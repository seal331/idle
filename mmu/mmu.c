/*
 * IDLE : Incomplete Draft of a Lisa Emulator 
 * 
 * File :  mmu.c 
 *         memory handling and i/o entry point
 *
 *
 */
static char     sccsid[] = "$Id: mmu.c,v 1.25 2008/04/09 20:53:26 gilles_fetis Exp $";
#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <sys/malloc.h>
#else
#include <malloc.h>
#endif
#include <memory.h>

#include "../cpu/m68k.h"
// for Bus error
#include "../cpu/m68kcpu.h"

#include "../irq/irq.h"
#include "../time/timer.h"

#include "mmu.h"

#include "../cpu/debug.h"

// the ROM and RAM spaces
uint8           *ramBase, *romBase, *videoRomBase;
uint8           *ramErrors;

#ifdef _MEMORY_WRITE_DEBUG_
uint32           *pcWhenWrite;
uint32           *addrWhenWrite;
#endif


// the MMU description
static int mmuSTART;
static int mmuCONTEXT=0;

// the Lisa video register
static int videoADDR=0;
static int videoADDR15=0;

// video irq enable
static int mmuVIDINTR=0;
static int mmuSTATUS=0x000F;
static int mmuMEMERR=0x0000;


static int mmuDIAG=0;
static int mmuMSK=0;

// we use a char[2] to avoid big/little endian issue
// the emulator stick to real mmu structure
// performance is not the goal (yet)
typedef struct mmuSegment {
        uint8 SOR[2];
        uint8 SLR[2];
        } mmuSegment;
        
#define MMU_CONTEXT_NUM 4
#define MMU_SEGMENT_BY_CONTEXT 128

mmuSegment mmu[MMU_CONTEXT_NUM][MMU_SEGMENT_BY_CONTEXT];

#define MEM_IS_ROM 0
#define MEM_IS_RAM_RW 1
#define MEM_IS_RAM_RO 2
#define MEM_IS_IO 3
#define MEM_IS_BUS_ERROR 4
#define MEM_IS_SOR 5
#define MEM_IS_SLR 6
#define MEM_IS_SN 7
#define MEM_IS_UNMAPPED 8
#define MEM_IS_BUST 9



/*
 * memory access jump tables
 */
void            (*mem_set_b) (uint32 address, uint8 value);
void            (*mem_set_w) (uint32 address, uint16 value);
void            (*mem_set_l) (uint32 address, uint32 value);
uint8           (*mem_get_b) (uint32 address);
uint16          (*mem_get_w) (uint32 address);
uint32          (*mem_get_l) (uint32 address);


uint32 clock68k;
static int lisaMMU(uint32 address,uint8** physaddress);


void alert_parity(int address) {
     IDLE_INIT_FUNC("alert_parity()");
     IDLE_TRACE("Parity x%06x=%d",address,ramErrors[address]);
}

void alert_setparity(int address) {
     IDLE_INIT_FUNC("alert_setparity()");
     IDLE_TRACE("Parity x%06x=%d",address,mmuDIAG);
}

INLINE void check_parity(int physaddress) {
       if ((ramErrors[physaddress] & mmuMSK)!=0) {
          alert_parity(physaddress);
            if (ramErrors[physaddress]&1)
               mmuSTATUS&=(~1);
            if (ramErrors[physaddress]&2)
               mmuSTATUS&=(~2);
            mmuMEMERR=(physaddress>>5)&0xFFFE;
			m68k_set_irq(M68K_IRQ_7);
       }
}

INLINE void set_parity(int physaddress) {
       ramErrors[physaddress]=mmuDIAG;
#ifdef _DEBUG_MMU_
       if (mmuDIAG)
              alert_setparity(physaddress);
#endif
}   

static int redraw_dirty;

void redraw_lisa_byte(uint32 video_offset);

INLINE void check_redraw_byte(int physaddress) {
       int delta=physaddress-videoADDR15;
       if ((delta>=0) && (delta<32760)) {
          redraw_dirty=1;
          redraw_lisa_byte(delta);
       }
}

INLINE void check_redraw_word(int physaddress) {
       int delta=physaddress-videoADDR15;
       if ((delta>=0) && (delta<32760)) {
          redraw_dirty=1;
          redraw_lisa_byte(delta);
          redraw_lisa_byte(delta+1);
       }
}

int must_redraw(void) {
    if (redraw_dirty) {
       redraw_dirty=0;
       return 1;
    }
    return 0;
}

void refresh_mmu_box(void)
{
     char buf[256];
     int context,seg;
     mmu_box[0]='\0';
     sprintf(buf,"MMU START=%d CTX=%d\n",mmuSTART,mmuCONTEXT);
     strcat(mmu_box,buf);  
     for (int context=0;context<4;context++)
         for (int seg=0;seg<128;seg++)
         {
             sprintf(buf,"[%d][%02x] or%03x l%02x t%01x\n",
                                     context,
                                     seg*2,
                                     ((mmu[context][seg].SOR[0])<<8|mmu[context][seg].SOR[1])&0xFFF,
                                     (mmu[context][seg].SLR[1]),
                                     (mmu[context][seg].SLR[0]&0x0F));
             strcat(mmu_box,buf);  
         }                           
 }

char *get_address_info_phys(uint32 address) {
     static char ret[1024];
     ret[0]='\0';
     if (address>=0x200000) {
        sprintf(ret,"physical address x%06x is out of range",
        address);
        return ret;
     }
#ifdef _MEMORY_WRITE_DEBUG_
       sprintf(ret,"Info for physical address x%06x set at PC=x%06x accessed addr=x%08x",
       address,
       pcWhenWrite[address],
       addrWhenWrite[address]);
#endif
      return ret;
}

char *get_address_info_mmu(uint32 mmu_address) {
     static char ret[1024];
     uint8 *trans_address;
     uint32 address;
     ret[0]='\0';
     if (lisaMMU(mmu_address,&trans_address)!=MEM_IS_RAM_RW) {
       sprintf(ret,"Logical address x%06x is not RAM !!!",mmu_address);
       return ret;
     }
     address=(trans_address-ramBase);
#ifdef _MEMORY_WRITE_DEBUG_
     sprintf(ret,"Info for physical address x%06x set at PC=x%06x accessed addr=x%08x",
     address,
     pcWhenWrite[address],
     addrWhenWrite[address]);
#endif
      return ret;
}

// IO Space handling
// define a 64Kb adress space for IO
#define IO_SPACE_MAX 0x10000
void (* ioTable[IO_SPACE_MAX]) (int *ioval,int rwMode,int size);
void (* ioTableAdr[IO_SPACE_MAX]) (uint32 adr,int *ioval,int rwMode,int size);


// IO access entry point
void accessIo(uint32 address,int *ioval,int rwMode,int size)
{
     idle_time delta;
     IDLE_INIT_FUNC("accessIo()");
     if (ioTable[address&0xFFFF]!=NULL)
        // jump to registered function (if any)
        ioTable[address&0xFFFF](ioval,rwMode,size);
     else
     {
         if (ioTableAdr[address&0xFFFF]!=NULL)
         {
                 ioTableAdr[address&0xFFFF](address,ioval,rwMode,size);
         }
         else
         {
             switch (address&0xF000) {
                    case 0x0000 :
                    case 0x1000 :
                         IDLE_WARN("Slot #1 low decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    case 0x2000 :
                    case 0x3000 :
                         IDLE_WARN("Slot #1 high decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    case 0x4000 :
                    case 0x5000 :
                         IDLE_WARN("Slot #2 low decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    case 0x6000 :
                    case 0x7000 :
                         IDLE_WARN("Slot #2 high decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    case 0x8000 :
                    case 0x9000 :
                         IDLE_WARN("Slot #3 low decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    case 0xA000 :
                    case 0xB000 :
                         IDLE_WARN("Slot #3 high decode io=%06x pc=%06x",address,get_pc);
                         if (rwMode==IO_READ)
                            *ioval=0;
                         break;
                    default:
                            IDLE_WARN("unexpected io=%06x pc=%06x",address,get_pc);
                            if (rwMode==IO_READ)
                            if (size==IO_BYTE)
                               *ioval=0xff;
                            else
                                *ioval=0xffff;
             } // of switch
             // reach timeout after 300ms
             delta=double2time(0.3);
             idle_timer_update_clock(delta);
              
             if (rwMode==IO_READ)
             {
              // mark bus timeout in status
                    mmuSTATUS&=(~8);
                    m68ki_bus_error(address, MODE_READ);
             }
             else
             {
              // mark bus timeout in status
                    mmuSTATUS&=(~8); 
                    m68ki_bus_error(address, MODE_WRITE);
             }
          } // of else
     } // of else
}

void initIo(void) 
{
     int i;
     for (i=0;i<IO_SPACE_MAX;i++) {
         ioTable[i]=NULL;
         ioTableAdr[i]=NULL;
         }
}

// register an IO function
void registerIoFunc(uint32 address,void (*func)(int*,int,int))
{
     ioTable[address]=func;
 }

void registerIoFuncAdr(uint32 address,void (*func)(uint32,int*,int,int))
{
     ioTableAdr[address]=func;
 }

// the IO funcs attached to CPU Card
void setSTART(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSTART()");
     mmuSTART=1; 
     IDLE_DEBUG("Start Mode pc=%06x",get_pc);                 
}
void resetSTART(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetSTART()");
     mmuSTART=0; 
     IDLE_DEBUG("Back mapped mode get_pc=%06x",get_pc); 
}

void setSEG1(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSEG()");
     mmuCONTEXT|=1;
     IDLE_DEBUG("new context %x",mmuCONTEXT);
}
void resetSEG1(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSEG()");
     mmuCONTEXT&=2;
     IDLE_DEBUG("new context %x",mmuCONTEXT);
}
void setSEG2(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSEG()");
     mmuCONTEXT|=2;
     IDLE_DEBUG("new context %x",mmuCONTEXT);
}
void resetSEG2(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSEG()");
     mmuCONTEXT&=1;
     IDLE_DEBUG("new context %x",mmuCONTEXT);
}

void setVIDEOADDR(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setVIDEOADDR()");
     if (size==IO_WORD)
        IDLE_WARN("is WORD");
     if (mode==IO_WRITE)
     {
         videoADDR=(*val)&0x3F;
         videoADDR15=videoADDR<<15;
     }
     else
     {
         *val=videoADDR;
     }
}
int isVBL=0;

void setVIDINTR(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setVIDINTR()");
     IDLE_DEBUG("Called");
     mmuVIDINTR=1;
    }

void resetVIDINTR(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetVIDINTR()");
     IDLE_DEBUG("Called");
     mmuVIDINTR=0;
     mmuSTATUS|=4;
     reset_irq_line(VBLIRQ);
}

void setDIAG1(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setDIAG1()");
     mmuDIAG|=1; 
     IDLE_DEBUG("Set DIAG1 pc=%06x",get_pc);                 
}
void resetDIAG1(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetDIAG1()");
     mmuDIAG&=(!1); 
     IDLE_DEBUG("Reset DIAG1 pc=%06x",get_pc); 
}

void setDIAG2(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setDIAG2()");
     mmuDIAG|=2; 
     IDLE_DEBUG("Set DIAG2 pc=%06x",get_pc);                 
}
void resetDIAG2(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetDIAG2()");
     mmuDIAG&=(!2); 
     IDLE_DEBUG("Reset DIAG2 pc=%06x",get_pc); 
}

void setSFMSK(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setSFMSK()");
     mmuMSK|=1; 
     IDLE_DEBUG("Set SFMSK pc=%06x",get_pc);                 
}
void resetSFMSK(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetSFMSK()");
     mmuMSK&=(!1); 
     IDLE_DEBUG("Reset SFMSK pc=%06x",get_pc); 
}

void setHDMSK(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("setHDMSK()");
     mmuMSK|=2; 
     IDLE_DEBUG("Set HDMSK pc=%06x",get_pc);                 
}
void resetHDMSK(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("resetHDMSK()");
     mmuMSK&=(!2); 
     IDLE_DEBUG("Reset HDMSK pc=%06x",get_pc); 
}

int endVBL(void)
{
     mmuSTATUS|=4;
     isVBL=0;
     return (mmuVIDINTR);
}

int beginVBL(void)
{
     isVBL=1;
     if (mmuVIDINTR)
          mmuSTATUS&=0xFFFB;
     return mmuVIDINTR;
}

void beginHBL(void) {
     mmuSTATUS&=(~0x20);
}

void endHBL(void) {
     mmuSTATUS|=0x20;
}


void getSTATUS(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("getSTATUS");
     if (mode==IO_WRITE)
     {
        IDLE_WARN("is written");
        return;
     }
     else
     if (size==IO_WORD)
     {
         *val=mmuSTATUS&0xFFFF;
     }
     else
     // read high byte
     *val=(mmuSTATUS&0xFF00)>>8;
}

void getSTATUSodd(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("getSTATUSodd()");
     if (mode==IO_WRITE)
     {
        IDLE_TRACE("Write status");
        return;
     }
     else
     if (size==IO_BYTE)
       *val=(mmuSTATUS&0xFF);
}

void getMEMERR(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("getMEMERR");
     if (mode==IO_WRITE)
     {
        IDLE_WARN("is written");
        return;
     }
     else
     if (size==IO_WORD)
     {
         *val=mmuMEMERR&0xFFFF;
     }
     else
     // read high byte
     *val=(mmuMEMERR&0xFF00)>>8;
     
     // mark bus timeout in status
     mmuSTATUS|=0x000F;
     IDLE_TRACE("Read memerr = x%04x",*val);
}


uint8* getVideoMemory(void)
{
       return (&ramBase[videoADDR15]);
       }

void ioVOID(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("ioVOID()");
     IDLE_TRACE("Called");
     *val=0xffff;
}

void ioCONTRAST(int* val,int mode,int size)
{
     IDLE_INIT_FUNC("ioCONTRAST()");
     IDLE_TRACE("Called");
}

void initMMUIo(void)
{
     int i;

     registerIoFunc(0xE000,&resetDIAG1);
     registerIoFunc(0xE002,&setDIAG1);
     registerIoFunc(0xE004,&resetDIAG2);
     registerIoFunc(0xE006,&setDIAG2);

     registerIoFunc(0xE008,&resetSEG1);
     registerIoFunc(0xE00A,&setSEG1);
     registerIoFunc(0xE00C,&resetSEG2);
     registerIoFunc(0xE00E,&setSEG2);

     registerIoFunc(0xE010,&setSTART);
     registerIoFunc(0xE012,&resetSTART);

     registerIoFunc(0xE014,&resetSFMSK);
     registerIoFunc(0xE016,&setSFMSK);

     registerIoFunc(0xE018,&resetVIDINTR);
     registerIoFunc(0xE01A,&setVIDINTR);

     registerIoFunc(0xE01C,&resetHDMSK);
     registerIoFunc(0xE01E,&setHDMSK);


     // on my lisa this range respond 0xffff 
     // for (i=0;i<0x200;i++) {
     //     registerIoFunc(0xD000+i,&ioVOID);
     //}
     
     for (i=0;i<0x800;i+=2) {
         registerIoFunc(0xE800+i,&setVIDEOADDR);
         registerIoFunc(0xF800+i,&getSTATUS);
         registerIoFunc(0xF801+i,&getSTATUSodd);   

         registerIoFunc(0xF000+i,&getMEMERR);
     }
}

/*
* Lisa mmu entry points
*/

uint8 unmapped[4]={0xff,0xff,0xff,0xff};

// great hexa initialisation value... found in AIX OS
uint32 lastError=0xDEADBEEF;
#define MIN_PHYS 0x080000
#define MAX_PHYS 0x180000

INLINE int lisaMMU(uint32 address,uint8** physaddress)
{
#ifdef _DEBUG_MMU_
    IDLE_INIT_FUNC("lisaMMU()");
#endif
       // if START mode and bit 14 not set   
       if ((mmuSTART) && ((address & 0x4000)==0x0000))
       {
            // mask bits 15 and 3
            switch (address & 0x18008) {
                   // bit 15 not set => ROM
                   case 0x0000:
                   case 0x0008:
                        *physaddress=&romBase[address&0x3FFF];
#ifdef _DEBUG_MMU_
                        IDLE_DEBUG("IS ROM");
#endif
                        return MEM_IS_ROM;
                   // bit 15 set and bit 3 set => SOR
                   case 0x8008:
                        mmu[mmuCONTEXT][(address&0xFE0000)>>17].SOR[0]&=0x0F;
                        *physaddress=(uint8*)&mmu[mmuCONTEXT][(address&0xFE0000)>>17].SOR; 
#ifdef _DEBUG_MMU_
                        IDLE_DEBUG("IS SOR");
#endif
                        return MEM_IS_SOR;
                   // bit 15 set and bit 3 not set => SLR
                   case 0x8000:
                        mmu[mmuCONTEXT][(address&0xFE0000)>>17].SLR[0]&=0x0F;
                        *physaddress=(uint8*)&mmu[mmuCONTEXT][(address&0xFE0000)>>17].SLR; 
#ifdef _DEBUG_MMU_
                        IDLE_DEBUG("IS SLR");
#endif
                        return MEM_IS_SLR;
                   default:
#ifdef _DEBUG_MMU_
                           IDLE_TRACE("Gross problem");
#endif
                           return MEM_IS_BUS_ERROR;
                   } // of switch
            } // of if (mmuSTART...
       // normal MMU mapped operation
       {
          uint32 seg= (address&0xFE0000)>>17;
          uint32 pageAdr=(address&0x01FE00);
          uint32 page=pageAdr>>9;
          uint32 offset=(address&0x1FF);
          int context;
          uint32 sorg;
          uint32 slim;
          
          // if supervisor mode, stick to 0 context (this enable interrupt to go to OS context)
           if (GetS())
             context=0;
         else 
              context=mmuCONTEXT;
              
          sorg=(uint32)(((((mmu[context][seg].SOR[0])<<8)|
                                   mmu[context][seg].SOR[1])
                                   &0xFFF)<<9);
          slim=(uint32)mmu[context][seg].SLR[1];
          switch (mmu[context][seg].SLR[0]&0xF) {
                 uint32 phys;
                 // RAM RO stack
                 case 0x4 :
                      // segment limit check (stack)
                      if (((page+slim+1)&0x100)==0x000)
                      {
#ifdef _DEBUG_MMU_
                         IDLE_WARN("IS LIMIT ERROR STACK RO logical=%06x page=%03x ctxt=%x",
                         address,
                         page,
                         context);
#endif
                         return MEM_IS_BUS_ERROR;
                      }
                      phys=((sorg+pageAdr)&0x1FFE00);
                      if ((phys>=MIN_PHYS) && (phys<MAX_PHYS))
                             *physaddress=&ramBase[((sorg+pageAdr)&0x1FFE00)|offset];
                      else {
#ifdef _DEBUG_MMU_
                           IDLE_TRACE("out of range RAM logical=%06x phys=%06x ctxt=%x",
                           address,
                           ((sorg+pageAdr)&0x1FFE00)|offset,
                           context
                           );
#endif
                           ramBase[0x180000]=0x00;
                           ramBase[0x180001]=0x00;
                           *physaddress=&ramBase[0x180000];
                      }
#ifdef _DEBUG_MMU_
                      IDLE_DEBUG("IS RAM STACK RO");
#endif
                      return MEM_IS_RAM_RO;
                      break;
                 // RAM RO
                 case 0x5 :
                      // segment limit check
                      if (((page+slim)&0x100)==0x100)
                      {
#ifdef _DEBUG_MMU_
                         IDLE_WARN("IS LIMIT ERROR RAM RO logical=%06x page=%03x ctxt=%x",
                         address,
                         page,
                         context);
#endif
                         return MEM_IS_BUS_ERROR;
                      }
                      phys=((sorg+pageAdr)&0x1FFE00);
                      if ((phys>=MIN_PHYS) && (phys<MAX_PHYS))
                             *physaddress=&ramBase[((sorg+pageAdr)&0x1FFE00)|offset];
                      else {
#ifdef _DEBUG_MMU_
                           IDLE_TRACE("out of range RAM logical=%06x phys=%06x ctxt=%x",
                           address,
                           ((sorg+pageAdr)&0x1FFE00)|offset,
                           context
                           );
#endif
                           ramBase[0x180000]=0x00;
                           ramBase[0x180001]=0x00;
                           *physaddress=&ramBase[0x180000];
                     }
#ifdef _DEBUG_MMU_
                      IDLE_DEBUG("IS RAM RO");
#endif
                      return MEM_IS_RAM_RO;
                      break;
                 // RAM RW stack
                 case 0x6 :
                      // segment limit check (stack)
                      if (((page+slim+1)&0x100)==0x000)
                      {
#ifdef _DEBUG_MMU_
                         IDLE_WARN("IS LIMIT ERROR STACK RW logical=%06x page=%03x ctxt=%x",
                         address,
                         page,
                         context);
#endif
                         return MEM_IS_BUS_ERROR;
                         }
                      phys=((sorg+pageAdr)&0x1FFE00);
                      if ((phys>=MIN_PHYS) && (phys<MAX_PHYS))
                             *physaddress=&ramBase[((sorg+pageAdr)&0x1FFE00)|offset];
                      else {
#ifdef _DEBUG_MMU_
                           IDLE_TRACE("out of range RAM logical=%06x phys=%06x ctxt=%x",
                           address,
                           ((sorg+pageAdr)&0x1FFE00)|offset,
                           context
                           );
#endif
                           ramBase[0x180000]=0x00;
                           ramBase[0x180001]=0x00;
                           *physaddress=&ramBase[0x180000];
                      }
#ifdef _DEBUG_MMU_
                         IDLE_DEBUG("IS RAM STACK RW");
#endif
                         return MEM_IS_RAM_RW;
                      break;
                 // RAM RW
                 case 0x7 :
                      // segment limit check
                      if (((page+slim)&0x100)==0x100)
                      {
#ifdef _DEBUG_MMU_
                         IDLE_WARN("IS LIMIT ERROR RAM RW logical=%06x page=%03x ctxt=%x",
                         address,
                         page,
                         context);
#endif
                         return MEM_IS_BUS_ERROR;
                      }
                      phys=((sorg+pageAdr)&0x1FFE00);
                      if ((phys>=MIN_PHYS) && (phys<MAX_PHYS))
                             *physaddress=&ramBase[((sorg+pageAdr)&0x1FFE00)|offset];
                      else {
#ifdef _DEBUG_MMU_
                           IDLE_TRACE("out of range RAM logical=%06x phys=%06x ctxt=%x",
                           address,
                           ((sorg+pageAdr)&0x1FFE00)|offset,
                           context
                           );
#endif
                           ramBase[0x180000]=0x00;
                           ramBase[0x180001]=0x00;
                           *physaddress=&ramBase[0x180000];
                      }
#ifdef _DEBUG_MMU_
                     IDLE_DEBUG("IS RAM RW");
#endif
                         return MEM_IS_RAM_RW;
                      break;
                 // IO space
                 case 0x8 :
                 case 0x9 : 
                        // segment limit check
                      if (((page+slim)&0x100)==0x100)
                      {
#ifdef _DEBUG_MMU_
                         IDLE_WARN("IS LIMIT ERROR IO");
#endif
                         return MEM_IS_BUS_ERROR;
                      }
#ifdef _DEBUG_MMU_
                     IDLE_DEBUG("IS IO");
#endif
#ifdef _DEBUG_MMU_
                     if (sorg!=0)
                        IDLE_TRACE("!!! io sorg=x%x",sorg);
#endif
                      return MEM_IS_IO;
                      break;
                 case 0xC :
#ifdef _DEBUG_MMU_
                     IDLE_WARN("IS UNMAPPED %06x seg=%x get_pc=%06x",address,seg,get_pc);
#endif
                     return MEM_IS_BUS_ERROR;
                 // ROM
                 case 0xF :
                      // serial num 
                      if ((pageAdr==0x8000) || (pageAdr==0xC000))
                      {
                         // derived from Raphael Nabet's unfinished MESS lisa driver
                         static int count=0;
                         uint8 SN;
                         if (!isVBL) { 
                               SN=videoRomBase[(count%56)];
#ifdef _DEBUG_MMU_
                               IDLE_DEBUG("Reading x%x %d",address,count);
#endif
                         }
                         else {
                               SN=videoRomBase[(count%56)|0x80];
#ifdef _DEBUG_MMU_
                               IDLE_DEBUG("Reading x%x %d",address,count+0x80);
#endif
                         }
                         count++;
                         if (count==56) count=0;
                         // in this case, SOR or SLIM of the current segment is selected
                         // (not sure it's really used anywhere...)
                         if ((address&0x000008)==0x0000008) {
                             mmu[context][seg].SOR[0]&=0x0F;
                             mmu[context][seg].SOR[0]|=(SN&0x80);
                            *physaddress=(&mmu[context][seg].SOR[0]);
                            return MEM_IS_SOR;
                         }
                         else {
                             mmu[context][seg].SLR[0]&=0x0F;
                             mmu[context][seg].SLR[0]|=(SN&0x80);
                            *physaddress=(&mmu[context][seg].SLR[0]);
                            return MEM_IS_SLR;
                         }
                         }
                      else
                        *physaddress=&romBase[(pageAdr+offset)&0x3FFF];
#ifdef _DEBUG_MMU_
                     IDLE_DEBUG("IS ROM2");
#endif
                        return MEM_IS_ROM;
                 default:
#ifdef _DEBUG_MMU_
                     IDLE_WARN("IS ERROR %d",mmu[context][seg].SLR[0]&0xF);
#endif
                       return MEM_IS_BUS_ERROR;
          } // of switch
       } // of normal MMU mode
       return MEM_IS_BUS_ERROR;
}


INLINE uint8 LisaGetB(uint32 address)
{
      int ret;
      uint8* physAddress;
      int ioVal;
#ifdef _DEBUG_MMU_
      IDLE_INIT_FUNC("LisaGetB()");
#endif
      ret=lisaMMU(address,&physAddress);
      switch (ret) {
             case MEM_IS_ROM :
                    return physAddress[0];
             case MEM_IS_RAM_RW :
             case MEM_IS_RAM_RO :
                    check_parity(physAddress-ramBase);
                    return physAddress[0];
             case MEM_IS_SOR :
             case MEM_IS_SLR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                    if ((address&0x1)==0)
                    {
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("MMU %s read %02x address=%06d" ,((ret==MEM_IS_SOR)?"SOR":"SLR"),physAddress[0],address);
#endif
                    return physAddress[0];
                    }
                    else
                    {
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("MMU %s read %02x address=%06d" ,((ret==MEM_IS_SOR)?"SOR":"SLR"),physAddress[1],address);
#endif
                    return physAddress[1];
                    }
             case MEM_IS_IO :
                    accessIo(address,&ioVal,IO_READ,IO_BYTE);
                    return (uint8)ioVal;
             case MEM_IS_SN:
                  // fixme : the read here is strange...
#ifdef _DEBUG_MMU_
                  IDLE_WARN("Should not read byte address=%06x",address);
#endif
                  return (0);
             case MEM_IS_BUS_ERROR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                    m68ki_bus_error(address, MODE_READ);
                     break;
             default :
                    mmuSTATUS&=(~8);
                    m68ki_bus_error(address, MODE_READ);
             }
}
INLINE uint16 LisaGetW(uint32 address)
{
      int ret;
      uint8* physAddress;
      int ioVal;
#ifdef _DEBUG_MMU_
      IDLE_INIT_FUNC("LisaGetW()");
#endif

      ret=lisaMMU(address,&physAddress);
      switch (ret) {
             case MEM_IS_SLR :
             case MEM_IS_SOR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
                    IDLE_DEBUG("MMU %s read %02x address=%06x" ,((ret==MEM_IS_SOR)?"SOR":"SLR"),(physAddress[0]<<8)|physAddress[1],address);
#endif
             case MEM_IS_ROM :
                    return (physAddress[0]<<8)|physAddress[1];
             case MEM_IS_RAM_RW :
             case MEM_IS_RAM_RO :
                    check_parity(physAddress-ramBase);
                    return (physAddress[0]<<8)|physAddress[1];
             case MEM_IS_SN:
                  return ((physAddress[0]&0x80)<<8);
             case MEM_IS_IO :
                    accessIo(address,&ioVal,IO_READ,IO_WORD);
                    return (uint16)ioVal;
             case MEM_IS_BUS_ERROR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                    m68ki_bus_error(address, MODE_READ);
                     break;
             default :
                    mmuSTATUS&=(~8);
                    m68ki_bus_error(address, MODE_READ);
             }
}

uint16 LisaGetWDebug(uint32 address)
{
      int ret;
      uint8* physAddress;
      int ioVal;
      if ((address&0x1)==0x1) 
      {
// fixme
         return 0;
      }
      ret=lisaMMU(address,&physAddress);
      switch (ret) {
             case MEM_IS_ROM :
             case MEM_IS_RAM_RW :
             case MEM_IS_RAM_RO :
             case MEM_IS_SOR :
             case MEM_IS_SLR :
                    return (physAddress[0]<<8)|physAddress[1];
             case MEM_IS_IO :
                    return (uint16)0xDEAD;
             case MEM_IS_BUS_ERROR :
                    return (uint16)0xDEAD;
             default :
                    return (uint16)0xDEAD;
             }
}
uint16 LisaGetWDebugCtxt(uint32 address,int ctxt)
{
      int ret;
      uint8* physAddress;
      int ioVal;
      int memo_context=mmuCONTEXT;
      mmuCONTEXT=ctxt;
      ret=lisaMMU(address,&physAddress);
      mmuCONTEXT=memo_context;
      switch (ret) {
             case MEM_IS_ROM :
             case MEM_IS_RAM_RW :
             case MEM_IS_RAM_RO :
             case MEM_IS_SOR :
             case MEM_IS_SLR :
                    return (physAddress[0]<<8)|physAddress[1];
             case MEM_IS_IO :
                    return (uint16)0xDEAD;
             case MEM_IS_BUS_ERROR :
                    return (uint16)0xDEAD;
             default :
                    return (uint16)0xDEAD;
             }
}
INLINE void LisaSetB(uint32 address,uint8 value)
{
      int ret;
      uint8* physAddress;
      int ioVal;
#ifdef _DEBUG_MMU_
      IDLE_INIT_FUNC("LisaSetB()");
#endif
      ret=lisaMMU(address,&physAddress);
      switch (ret) {
             case MEM_IS_ROM :
             case MEM_IS_RAM_RO :
#ifdef _DEBUG_MMU_
                     IDLE_DEBUG("Write to read only at x%x",address);
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                     IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                     m68ki_bus_error(address, MODE_WRITE);
                     break;
             case MEM_IS_RAM_RW :
                    set_parity(physAddress-ramBase);
#ifdef _MEMORY_WRITE_DEBUG_
                    pcWhenWrite[physAddress-ramBase]=m68k_get_reg(NULL,M68K_REG_PPC);
                    addrWhenWrite[physAddress-ramBase]=(address&0xFFFFFF)|(mmuCONTEXT<<24);
#endif
                    physAddress[0]=value;
                    check_redraw_byte(physAddress-ramBase);
                    return;
             case MEM_IS_SOR :
             case MEM_IS_SLR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
                    IDLE_DEBUG("!!! MMU BYTE %s write %02x address=%06x" ,((ret==MEM_IS_SOR)?"SOR":"SLR"),value,address);
#endif
                    if ((address&0x01)==0)
                    {
                       physAddress[0]=value;
                       return;
                    }
                    else
                    {
                       physAddress[1]=value;
                       return;
                    }
             case MEM_IS_IO :
                    ioVal=value;
                    accessIo(address,&ioVal,IO_WRITE,IO_BYTE);
                    return;
             case MEM_IS_BUS_ERROR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                     IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                     m68ki_bus_error(address, MODE_WRITE);
                     break;
             default :
                    mmuSTATUS&=(~8);
                     m68ki_bus_error(address, MODE_WRITE);
             }
}
INLINE void LisaSetW(uint32 address,uint16 value)
{
      int ret;
      uint8* physAddress;
      int ioVal;
#ifdef _DEBUG_MMU_
      IDLE_INIT_FUNC("LisaSetW()");
#endif
      ret=lisaMMU(address,&physAddress);
      switch (ret) {
             case MEM_IS_RAM_RO : 
             case MEM_IS_ROM :
#ifdef _DEBUG_MMU_
                     IDLE_DEBUG("Write to read only at x%x",address);
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                     IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                     m68ki_bus_error(address, MODE_WRITE);
                     break;
             case MEM_IS_SOR :
             case MEM_IS_SLR :
                    physAddress[0]=((value&0xFF00)>>8);
                    physAddress[1]=value&0xFF;
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
                    IDLE_DEBUG("MMU %s write %04x address=%06x" ,((ret==MEM_IS_SOR)?"SOR":"SLR"),(physAddress[0]<<8)|physAddress[1],address);
#endif
                    break;
             case MEM_IS_RAM_RW :
                    set_parity(physAddress-ramBase);
                    physAddress[0]=((value&0xFF00)>>8);
                    physAddress[1]=value&0xFF;
                    check_redraw_word(physAddress-ramBase);
#ifdef _MEMORY_WRITE_DEBUG_
                    pcWhenWrite[physAddress-ramBase]=m68k_get_reg(NULL,M68K_REG_PPC);
                    addrWhenWrite[physAddress-ramBase]=(address&0xFFFFFF)|(mmuCONTEXT<<24);
                    pcWhenWrite[physAddress-ramBase+1]=m68k_get_reg(NULL,M68K_REG_PPC);
                    addrWhenWrite[physAddress-ramBase+1]=(address&0xFFFFFF)|(mmuCONTEXT<<24);
#endif
                    break;
             case MEM_IS_IO :
                    ioVal=value;
                    accessIo(address,&ioVal,IO_WRITE,IO_WORD);
                    return;
             case MEM_IS_BUS_ERROR :
#ifdef _DEBUG_MMU_
                    IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPPC)));
                     IDLE_DEBUG("opcode=%s",unassemble_addr(m68k_get_reg(NULL,M68K_REG_PPC)));
#endif
                     m68ki_bus_error(address, MODE_WRITE);
                     break;
             default :
                    mmuSTATUS&=(~8);
                     m68ki_bus_error(address, MODE_WRITE);
             }
}


INLINE void    LisaSetL (uint32 address, uint32 value)
{
    LisaSetW(address,(value&0xFFFF0000)>>16);
    LisaSetW(address+2,value&0xFFFF);
}

INLINE uint32  LisaGetL(uint32 address)
{
    uint32 ret;
    ret=(LisaGetW(address)<<16);
    ret|=LisaGetW(address+2);
    return ret;
}

char *
read_alt_rom(char * romName) {
    uint8 tmp_int[4];
    int nb_part;
    int start;
    int length;
    int part_loop,byte_loop;
    uint8 byte;
    FILE *f=fopen(romName,"rb");
    IDLE_INIT_FUNC("read_alt_rom");
    if (f==NULL) return "cannot open rom";

    if (fseek(f,4,SEEK_SET) <0) goto error;
    fread(tmp_int,2,1,f);
    nb_part=(tmp_int[0]<<8)|tmp_int[1];
    IDLE_TRACE("Rom is in %d parts",nb_part);

    if (fseek(f,20,SEEK_SET) <0) goto error;

    for (part_loop=0;part_loop<nb_part;part_loop++) {
        fread(tmp_int,4,1,f);
        start=(tmp_int[0]<<24)|(tmp_int[1]<<16)|(tmp_int[2]<<8)|tmp_int[3];
        fread(tmp_int,4,1,f);
        length=(tmp_int[0]<<24)|(tmp_int[1]<<16)|(tmp_int[2]<<8)|tmp_int[3];
        IDLE_TRACE("start=x%x length=x%x",start,length);
        for (byte_loop=0;byte_loop<length;byte_loop++) {
              byte=fgetc(f);      
              romBase[(start+byte_loop)&0x3FFF]=byte;
        }
        // word align...
        if ((length&0x0001)==0x0001)
           byte=fgetc(f);
    }
    fclose(f);
    return NULL;
error:
      fclose(f);
      return "fatal error alt rom";
}

char*            MemInit(int model)
{
    FILE *rom;
    int ret;
    uint8 romtmp[512*32];
    int i;

        //  
        ramBase=(uint8*)malloc(512*4096);
         
        // initial memory error support
        ramErrors=(uint8*)malloc(512*4096);
        for (i=0;i<512*4096;i++)
            ramErrors[i]=0;

#ifdef _MEMORY_WRITE_DEBUG_
       pcWhenWrite=(uint32*)malloc(512*4096*sizeof(uint32));
       addrWhenWrite=(uint32*)malloc(512*4096*sizeof(uint32));
#endif
            
        romBase=(uint8*)malloc(512*32);
        videoRomBase=(uint8*)malloc(256);
        
        if (read_alt_rom("bios/fake_booth.bin")!=NULL) {
        
            //rom=fopen("bios/booth.hi","rb");
            if (model==1) {
                rom=fopen("bios/L175REVC.bin","rb");
                if (rom==NULL) return "fatal error needs file bios/L175REVC.bin";
            } else {
                rom=fopen("bios/341-0175-h","rb");
                if (rom==NULL) return "fatal error needs file bios/341-0175-h";
            }
            
            ret=fread(&romtmp[0],512*16,1,rom);
            fclose(rom);
            
            //rom=fopen("bios/booth.lo","rb");
            if (model==1) {
                rom=fopen("bios/L176REVC.bin","rb");
                if (rom==NULL) return "fatal error needs file bios/L176REVC.bin";
            } else {
                rom=fopen("bios/341-0176-h","rb");
                if (rom==NULL) return "fatal error needs file bios/341-0176-h";
            }
            
            ret=fread(&romtmp[512*16],512*16,1,rom);
            fclose(rom);

            // reorder rom
            for (i=0;i<512*16;i++)
            {
             romBase[i*2]=romtmp[i];
             romBase[i*2+1]=romtmp[i+512*16];
            }

            // Patch the H rom (untill SCC is fixed...)
            // voids checksum        
            if (model==2) {
                romBase[0x01A8]=0x4E;
                romBase[0x01A9]=0x71;
                romBase[0x01AA]=0x4E;
                romBase[0x01AB]=0x71;

                
                // fixme jump after rs232
                romBase[0x1000]=0x60;
                romBase[0x1001]=0x00;
                romBase[0x1002]=0x01;
                romBase[0x1003]=0x0A;

            }        
        }
        rom=fopen("bios/vidstate.rom","rb");
        if (rom==NULL) return "fatal error needs file bios/vidstate.rom";
        ret=fread(&videoRomBase[0],256,1,rom);
        fclose(rom);

        mmuSTART=1;
        initIo();
        initMMUIo();

        
        
        return NULL;
}

void mmuReset(void)
{
     mmuSTART=1;
}

unsigned int  m68k_read_memory_8(unsigned int address) {
         return LisaGetB(address);
}
unsigned int  m68k_read_memory_16(unsigned int address){
         return LisaGetW(address);
}
unsigned int  m68k_read_memory_32(unsigned int address){
         return LisaGetL(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value){
         LisaSetB(address,(uint8) value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value){
         LisaSetW(address,(uint16) value);
}
void m68k_write_memory_32(unsigned int address, unsigned int value){
         LisaSetL(address,(uint32) value);
}
