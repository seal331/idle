/*

  Name: mmu/mmu.h
  Copyright: IDLE Incomplete Draft of a Lisa Emulator
  Author: G.Fetis
  Date: 11/10/06 09:40
  Description: public header of Lisa MMU

  $Log: mmu.h,v $
  Revision 1.13  2007/04/06 15:15:40  gilles_fetis
  more help when no rom file...

  Revision 1.12  2007/04/03 18:08:19  gilles_fetis
  more debug + improved SN

  Revision 1.11  2007/03/13 23:13:43  gilles_fetis
  less traces (+ a fix)

  Revision 1.4  2006/10/28 09:53:40  gilles_fetis
  added mapped io for disk

  Revision 1.3  2006/10/26 19:43:40  gilles_fetis
  initial disk support and line feed correction

  Revision 1.1  2006/10/12 10:05:12  gilles_fetis
  added initial via support

*/

#define IO_READ 0
#define IO_WRITE 1
#define IO_BYTE 0
#define IO_WORD 1

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint32 clock68k;

void registerIoFunc(uint32 address,void (*func)(int*,int,int));
void registerIoFuncAdr(uint32 address,void (*func)(uint32, int*,int,int));
uint8* getVideoMemory(void);
int beginVBL(void);
int endVBL(void);
void beginHBL(void);
void endHBL(void);
int must_redraw(void);
uint16 LisaGetWDebug(uint32 address);
uint16 LisaGetWDebugCtxt(uint32 address,int ctxt);
char*   MemInit(int model);
void setRreadWildIO(int mode);
void mmuReset(void);
void refresh_mmu_box(void);
char *get_address_info_phys(uint32 address);
char *get_address_info_mmu(uint32 mmu_address);

#ifdef __cplusplus
}
#endif
