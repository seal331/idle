/*
 * 05/10/2006 : debugger 
 * 07/10/2006 : first code executed => ROM checksum error
 * 08/10/2006 : _ checksum patched => MMU test pass but no RAM found
 *              _ added breakpoint
 * 09/10/2006 : _ added MMU debug
 *              _ corrected mmu & Long Write
 *              _ added lock to 0 context when in supervisor mode (for mmu use)
 *              _ now crash on first RTS. 0xFE06E0 (stack translation error?)
 *              _ => corrected HWReset (bad ssp)
 *              _ fail for VIA test 
 *              _ FE08A2 
 *              _ ROM was not broken, MemInit() was...
 *              _ corrected screen
 *              _ Now go graphical mode => IO Board error code 50
 *                (normal, VIA are not emulated yet).
 * 11/10/2006 : _ VIA partially emulated 
 *              => VIA test ok but crashes in castaway code (move to SR)
                => try SR_IMPLEMENTATION 2 instead of 0
                not better address of crash is FE09B2
 * 12/10/2006   _ workaround the error (the long_jump vector was not set in CPUStep)
                => will need to verify if long_jump is really needed.
                _ now io board error 52 (io board COPS error) => OK
 * 13/10/2006   _ added less primitive VIA support
                _ COPS test now ok
                _ error 42 (video error) due to lack of video interrupt
                _ FE0B96
                _ video interrupt now OK
                _ serial number getting error => should support videorom
   28/10/2006   _ fe1c96 : boot sector seems ok : error 47
   
   01/04/2007   _ added profile support
   04/04/2007   _ first LOS boot ;-)
   13/07/2007   _ XENIX working (without hang)
 */
#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _CAST_
#include "cpu/68000.h"
#else
#include "cpu/m68k.h"
#endif

#include "mmu/mmu.h"
#include "cpu/debug.h"
#include "io/via.h"
#include "io/8530scc.h"
#include "disk/disk_image.h"
#include "disk/disk_ctrl.h"
#include "time/timer.h"
#include "irq/irq.h"

#include "debug/mem_explorer.h"

int ask_bkpt=0;
static int exact=1;

#define PC_DEBUG_SIZE 8
uint32 pc_debug[PC_DEBUG_SIZE];
int last_pc_debug=0;

#define PCDBG() pc_debug[last_pc_debug]=get_pc;last_pc_debug=((last_pc_debug+1)%PC_DEBUG_SIZE)

void init();
void deinit();

static  int insert_dc42_image(int slot);
static  int insert_dart_image(void);
static int lisa_reset(void);

char debug_str[6*4+1];

DIALOG debug_popup[] =
{
   /* (dialog proc)     (x)   (y)   (w)   (h)  (fg) (bg) (key) (flags)     (d1) (d2)    (dp)                   (dp2) (dp3) */
   { d_shadow_box_proc,  20,  10,  280,  180,    0,   0,    0,    D_DISABLED, 0,   0,    (void*)NULL },
   { d_button_proc,     30,   30,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Add this func to debug" },
   // the breakpoint
   { d_edit_proc,       30, 50,  260,   16,   0,  0,    0,      0,       63,   0,    (void *)debug_str,       NULL, NULL  },
   { d_button_proc,    164,  170,  126,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Return" },
   { d_yield_proc,       0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  },
   { NULL,               0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  }
};

void debugPopup(void) {
    int ret;

   /* set up colors */
   gui_fg_color = makecol(0, 0, 0);
   gui_mg_color = makecol(128, 128, 128);
   gui_bg_color = makecol(200, 200, 200);
   set_dialog_color (debug_popup, gui_fg_color, gui_bg_color);


    clear_keybuf();
    ret=popup_dialog(debug_popup,-1);

    if (ret==1) {
                insert_dc42_image(0);
    }
    if (ret==2) {
                insert_dc42_image(1);
    }
    if (ret==3) {
                ask_bkpt=1;
    }
    
}


DIALOG options_popup[] =
{
   /* (dialog proc)     (x)   (y)   (w)   (h)  (fg) (bg) (key) (flags)     (d1) (d2)    (dp)                   (dp2) (dp3) */
   { d_shadow_box_proc,  20,  10,  280,  180,    0,   0,    0,    D_DISABLED, 0,   0,    (void*)NULL },
   { d_button_proc,     30,   30,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Set fastest speed" },
   { d_button_proc,     30,   50,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Set 5MHz speed" },
   { d_button_proc,     30,   70,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Turn Profile ON" },
   { d_button_proc,     30,   90,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Turn Profile OFF" },
   { d_button_proc,    164,  170,  126,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Return" },
   { d_yield_proc,       0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  },
   { NULL,               0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  }
};

void optionsPopup(void) {
    int ret;

   /* set up colors */
   gui_fg_color = makecol(0, 0, 0);
   gui_mg_color = makecol(128, 128, 128);
   gui_bg_color = makecol(200, 200, 200);
   set_dialog_color (options_popup, gui_fg_color, gui_bg_color);


    clear_keybuf();
    ret=popup_dialog(options_popup,-1);
    if (ret==1) {
        exact=0;
    }
    if (ret==2) {
        exact=1;
    }
    
    if (ret==3) {
        via_turn_on_PROFILE();
    }
    if (ret==4) {
        via_turn_off_PROFILE();
    }
}

int quit(void)
{
   if (alert("Really Quit?", NULL, NULL, "&Yes", "&No", 'y', 'n') == 1)
      return D_CLOSE;
   else
      return D_REDRAW;
}

int reset_or_turnout(void)
{
   if (alert("What to do?", NULL, NULL, "&RESET", "&Turn Off", 'R', 'T') == 1)
      return D_CLOSE;
   else
      return D_REDRAW;
}
DIALOG fast_popup[] =
{
   /* (dialog proc)     (x)   (y)   (w)   (h)  (fg) (bg) (key) (flags)     (d1) (d2)    (dp)                   (dp2) (dp3) */
   { d_shadow_box_proc,  20,  10,  280,  180,    0,   0,    0,    D_DISABLED, 0,   0,    (void*)NULL },
   { d_button_proc,     30,   30,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Disk image DC42 drive 1" },
   { d_button_proc,     30,   50,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Disk image DC42 drive 2" },
   { d_button_proc,     30,   80,  260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Call Debugger" },
   { d_button_proc,     30,   100, 260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Set options" },
   { d_button_proc,     30,   120, 260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"RESET / TURN OFF" },
   { d_button_proc,     30,   140, 260,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"EXIT (should try turn off first)" },
   { d_button_proc,    164,  170,  126,   16,    0,   0,    0,    D_EXIT,     0,   0,    (void*)"Return" },
   { d_yield_proc,       0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  },
   { NULL,               0,    0,    0,    0,    0,   0,    0,      0,        0,   0,    NULL,                   NULL, NULL  }
};

void fastPopup(void) {
    int ret;

   /* set up colors */
   gui_fg_color = makecol(0, 0, 0);
   gui_mg_color = makecol(128, 128, 128);
   gui_bg_color = makecol(200, 200, 200);
   set_dialog_color (fast_popup, gui_fg_color, gui_bg_color);


    clear_keybuf();
    ret=popup_dialog(fast_popup,-1);

    if (ret==1) {
                insert_dc42_image(0);
    }
    if (ret==2) {
                insert_dc42_image(1);
    }
    if (ret==3) {
                ask_bkpt=1;
    }
    if (ret==4) {
                optionsPopup();
    }
    if (ret==5) {
                 if   (reset_or_turnout()==D_CLOSE)
                      lisa_reset();
                 else
                     via_keyb_power_off();
    }
    if (ret==6) {
                if (quit()==D_CLOSE)
                   exit(0);
    }
    
}


int about(void)
{
   alert("IDLE (incomplete Draft of a Lisa Emulator)\nversion alpha 0.10- 2015"  
   , NULL, NULL, "&OK", "&No", 'y', 'n');
      return D_REDRAW;
}



char log_box_init[0x10000];

// switch to this area while disassembling

char log_box_init2[60*20*4+1];
char *log_box=log_box_init;

char mmu_box_init[0x10000];
char *mmu_box=mmu_box_init;

char sharedram_box_init[0x10000];
char *sharedram_box=sharedram_box_init;

char disass_box[60*20*4+1];


void disass_debugger(void) {
#ifdef _CAST_                  
                  uint32 l_pc=pc;
#else
                  uint32 l_pc=m68k_get_reg(NULL,M68K_REG_PC);
#endif
                  uint16 l_inststream[256];
                  int i,j;
                  int l;
                  int inst_size;
                  char l_inst[256];
                  char addr[256];
                  // clears the text
                  disass_box[0]='\0';
                  no_trace();
                  for (l=0;l<20;l++)
                  {
                      log_box[0]='\0';
                      // fills inst stream
                      for (i=0;i<32;i++)
                          l_inststream[i]=LisaGetWDebug(l_pc+i*2);
                      // 
                      sprintf(addr,"%06X ",l_pc);
                      strcat(disass_box,addr);
                      // disass one instruction
                      inst_size=disass(l_inst,l_inststream);
                      for (j=0;j<6;j++)
                      {
                          if (j<inst_size)
                             sprintf(addr,"%04X",l_inststream[j]);
                          else
                              strcpy(addr,"    ");
                          strcat (disass_box,addr);
                      }
                      // adds to box
                      strcat(disass_box,l_inst);
                      // plus a carriage return
                      strcat(disass_box,"\n");
                      l_pc+=(2*inst_size);
                  }
                  restore_trace();
     }


char registers_box[30*20*4+1];

void regs_debugger(void)
{
     char buf[256];
     int i;
     registers_box[0]='\0';

     sprintf(buf,"PC=x%08X SR=%s\n",get_pc,GetStatus());
     strcat(registers_box,buf);
     sprintf(buf,"D0=x%08X A0=x%08X\n",m68k_get_reg(NULL,M68K_REG_D0),m68k_get_reg(NULL,M68K_REG_A0));
     strcat(registers_box,buf);
     sprintf(buf,"D1=x%08X A1=x%08X\n",m68k_get_reg(NULL,M68K_REG_D1),m68k_get_reg(NULL,M68K_REG_A1));
     strcat(registers_box,buf);
     sprintf(buf,"D2=x%08X A2=x%08X\n",m68k_get_reg(NULL,M68K_REG_D2),m68k_get_reg(NULL,M68K_REG_A2));
     strcat(registers_box,buf);
     sprintf(buf,"D3=x%08X A3=x%08X\n",m68k_get_reg(NULL,M68K_REG_D3),m68k_get_reg(NULL,M68K_REG_A3));
     strcat(registers_box,buf);
     sprintf(buf,"D4=x%08X A4=x%08X\n",m68k_get_reg(NULL,M68K_REG_D4),m68k_get_reg(NULL,M68K_REG_A4));
     strcat(registers_box,buf);
     sprintf(buf,"D5=x%08X A5=x%08X\n",m68k_get_reg(NULL,M68K_REG_D5),m68k_get_reg(NULL,M68K_REG_A5));
     strcat(registers_box,buf);
     sprintf(buf,"D6=x%08X A6=x%08X\n",m68k_get_reg(NULL,M68K_REG_D6),m68k_get_reg(NULL,M68K_REG_A6));
     strcat(registers_box,buf);
     sprintf(buf,"D7=x%08X A7=x%08X\n",m68k_get_reg(NULL,M68K_REG_D7),m68k_get_reg(NULL,M68K_REG_A7));
     strcat(registers_box,buf);
     sprintf(buf,"CPU_MODE=x%x  STOPPED=x%x\n",m68k_get_reg(NULL,M68K_REG_CPU_RUN_MODE),m68k_get_reg(NULL,M68K_REG_CPU_STOPPED));
     strcat(registers_box,buf);

     for (i=1;i<9;i++) {
          sprintf(buf,"[n-%d = x%06x]",i,pc_debug[(last_pc_debug-i+PC_DEBUG_SIZE)%PC_DEBUG_SIZE]);
          strcat(registers_box,buf);
     }
 }


char breakpoint_str[6*4+1];

// one cpu instruction
int step(void)
{
    log_box[0]='\0';
    m68k_pulse_go();
    m68k_execute(1);
    disass_debugger();
    regs_debugger();
    refresh_mmu_box();
    refresh_sharedram_box();
    get_log_str(log_box,sizeof(log_box_init));
    return D_REDRAW;
}

// loop CPU instruction until PC is greater (use this for loop)
int stepover(void)
{

                  uint32 savedPC=m68k_get_reg(NULL,M68K_REG_PC);

    do {
       log_box[0]='\0';
     m68k_execute(1);
       
    } while (((m68k_get_reg(NULL,M68K_REG_PC)<=savedPC) || (m68k_get_reg(NULL,M68K_REG_PC)>savedPC+8)) && !key[KEY_SPACE]);
   while (key[KEY_SPACE]);
    disass_debugger();
    regs_debugger();
    refresh_mmu_box();
    refresh_sharedram_box();
    get_log_str(log_box,sizeof(log_box_init));
    return D_REDRAW;
}

volatile int vbl;

void vblupdate(void)
{
 vbl++;
}
END_OF_FUNCTION(vblupdate);

volatile int last_scancode;
      
void keypress_handler(int scancode)
{
     last_scancode=scancode;
} END_OF_FUNCTION(keypress_handler)


void lisa_keyb(void) {
     keyboard_lowlevel_callback = keypress_handler;
}

void allegro_keyb(void) {
      keyboard_lowlevel_callback = NULL;
      clear_keybuf();      
}

volatile int loop_exit=0;

/*
	79 7E 70 7D 74 68 78 75		; Z     Shft  A     ALck  1!    `~    Tab  Q
	7E 00 00 48 45 41 57 42 	; Shft              Retn  Bksp  =+    ]}   \|
	00 4C 5B 5A 40 51 44 56		;       /?    '"    ;:    -_    0)    P    [{
	00 00 00 00 00 00 00 00 
	00 00 00 00 00 00 00 00
	6E 6C 6A 69 64 73 65 66 	; B     V     G     F     5%    4$    R     T
	58 6F 54 6B 62 61 67 52		; M     N     J     H     7&    6^    Y     U
	5E 5D 59 55 50 63 53 5F 	; .>    ,<    L     K     9(    8*    I     O
	43 2C 49 4E 7F 7C 5C 46		; <>    .     0     ROptn Apple LOptn Spc   Enter
	6D 7A 7B 76 72 71 77 60 	; C     X     D     S     3#    2@    W     E
	2F 2E 2B 2A 23 22 26 27		; NRtn  3     +     6     =     *     9     /
	2D 4D 29 28 21 20 24 25		; 2     1     5     4     -     Clr   7     8
*/	
	
int pc2lisa_scancode(int code)
{
    int mode=(code&0x80)^0x80;
    int lkey;
    IDLE_INIT_FUNC("pc2lisa_scancode()");
    switch (code&0x7F) {
           case KEY_LCONTROL : lkey=0x7F ;break;
           case KEY_ALT : lkey=0x7F ;break;
           case KEY_ALTGR : lkey=0x68 ;break;
           case KEY_ENTER : lkey=0x48 ;break;
           case KEY_BACKSPACE : lkey=0x45 ;break;
           case KEY_LSHIFT : lkey=0x7E ;break;
           case KEY_RSHIFT : lkey=0x7E ;break;
           case KEY_TAB : lkey=0x78 ;break;


           case KEY_1 : lkey=0x74 ;break;
           case KEY_2 : lkey=0x71 ;break;
           case KEY_3 : lkey=0x72 ;break;
           case KEY_4 : lkey=0x73 ;break;
           case KEY_5 : lkey=0x64 ;break;
           case KEY_6 : lkey=0x61 ;break;
           case KEY_7 : lkey=0x62 ;break;
           case KEY_8 : lkey=0x63 ;break;
           case KEY_9 : lkey=0x50 ;break;
           case KEY_0 : lkey=0x51 ;break;
           case KEY_MINUS : lkey=0x40 ;break; 
           case KEY_EQUALS : lkey=0x41 ;break;
           
           case KEY_Q : lkey=0x75 ;break;
           case KEY_W : lkey=0x77 ;break;
           case KEY_E : lkey=0x60 ;break;
           case KEY_R : lkey=0x65 ;break;
           case KEY_T : lkey=0x66 ;break;
           case KEY_Y : lkey=0x67 ;break;
           case KEY_U : lkey=0x52 ;break;
           case KEY_I : lkey=0x53 ;break;
           case KEY_O : lkey=0x5f ;break;
           case KEY_P : lkey=0x44 ;break;
           
           case KEY_A : lkey=0x70 ;break;
           case KEY_S : lkey=0x76 ;break;
           case KEY_D : lkey=0x7b ;break;
           case KEY_F : lkey=0x69 ;break;
           case KEY_G : lkey=0x6a ;break;
           case KEY_H : lkey=0x6b ;break;
           case KEY_J : lkey=0x54 ;break;
           case KEY_K : lkey=0x55 ;break;
           case KEY_L : lkey=0x59 ;break;

           case KEY_Z : lkey=0x79 ;break;
           case KEY_X : lkey=0x7a ;break;
           case KEY_C : lkey=0x6d ;break;
           case KEY_V : lkey=0x6c ;break;
           case KEY_B : lkey=0x6e ;break;
           case KEY_N : lkey=0x6f ;break;
           case KEY_M : lkey=0x58 ;break;

           case KEY_SLASH : lkey=0x4C ;break;
           case KEY_COMMA : lkey=0x5D ;break;
           case KEY_STOP   : lkey=0x5E ;break;

           case KEY_OPENBRACE : lkey=0x56 ;break;
           case KEY_CLOSEBRACE   : lkey=0x57 ;break;
           

// there seem to exist a confusion
// in allegro for colon/semicolon
           case KEY_SEMICOLON :     
           case KEY_COLON : lkey=0x5A ;break;
           case KEY_QUOTE   : lkey=0x5B ;break;
           
           case KEY_TILDE : lkey=0x68 ;break;
           case KEY_BACKSLASH : lkey=0x42;break;
           
           case KEY_F12 : if (mode)
                             m68k_set_irq(M68K_IRQ_7);
                           else
                             m68k_set_irq(M68K_IRQ_NONE);
                          break;
           case KEY_F11: if (mode)
                              via_keyb_power_off();
                         break;
           case KEY_F10 : if (mode) {
                             IDLE_TRACE("Interrupt 6");
                             m68k_set_irq(M68K_IRQ_6);
                             }
                           else
                             m68k_set_irq(M68K_IRQ_NONE);
                           lkey=0x5c;
                          break;
           
           case KEY_F1 : exact=0;
                       lkey=0x5c;
                       break;
           case KEY_F2 : exact=1;
                         lkey=0x5c;
                        break;

           case KEY_F3 :lkey=0x48;
                        break;

           case KEY_F4 :lkey=0x4E;
                        break;

           case KEY_F5 :lkey=0x7C;
                        break;

           case KEY_F9 : loop_exit=1;
           // under allegro this will cause program end
           // case KEY_ESC : loop_exit=1;
           
           default : lkey=0x5c; // SPACE by default
                     IDLE_TRACE("Pressed 0x%x",code);
    }
    return (lkey|mode);
}

int getKeyEvent() {
    static int prev_code=0;
    int ret;
    int code=last_scancode; // pseudo atomic read
    if (prev_code!=code)
    {
       ret=via_keyb_event_key(pc2lisa_scancode(code));
       prev_code=code;
       return ret;
    }
    return 0;
}

int getMouseButton() {
    static int prev=0;
    int button=mouse_b&1;
    int ret;
    if (prev!=button)
    {
       if (button)
              ret=via_keyb_event_key(0x86);
       else
              ret=via_keyb_event_key(0x06);
       prev=button;
       return ret;
    }
    return 0;
}

static int dirty_rect[23][12];
static BITMAP *lisa_screen[2];
static int lisa_screen_cur=0;
static int lisa_screen_next=1;
static int screen_line;
     
void redraw_lisa_byte(uint32 video_offset) {
     int y,x,xx;
     uint8* video=getVideoMemory();
     int mask;
     int black=makecol(0, 0, 0);
     int white=makecol(255, 255, 255);
     
     x=video_offset%90;
     y=video_offset/90;
     acquire_bitmap(lisa_screen[0]);
     acquire_bitmap(lisa_screen[1]);
     if ((y>screen_line) && (dirty_rect[x/4][y/32]<2)) {
        dirty_rect[x/4][y/32]=1;    
        mask=0x80;
        for (xx=0;xx<8;xx++)
        {
            if ((video[video_offset]&mask)==mask)
               putpixel(lisa_screen[lisa_screen_cur],x*8+xx,y,black);
               else
               putpixel(lisa_screen[lisa_screen_cur],x*8+xx,y,white);
             mask=mask>>1;
          }
        }     
         else
              dirty_rect[x/4][y/32]=2;
         
        mask=0x80;
        for (xx=0;xx<8;xx++)
        {
            if ((video[video_offset]&mask)==mask)
               putpixel(lisa_screen[lisa_screen_next],x*8+xx,y,black);
               else
               putpixel(lisa_screen[lisa_screen_next],x*8+xx,y,white);
             mask=mask>>1;
         }
         
     release_bitmap(lisa_screen[0]);
     release_bitmap(lisa_screen[1]);
}

int redraw_lisa(int redraw_all)
{
    static int first=1;
    static int live=0;
    int y,x,xx;
    uint8* video=getVideoMemory();
    int mask;
    int black=makecol(0, 0, 0);
    int white=makecol(255, 255, 255);
    int red=makecol(255, 0, 0);
    if (first)
    {
         lisa_screen[0]=create_bitmap(720,364);
         lisa_screen[1]=create_bitmap(720,364);
         first=0;
    }
              
    acquire_bitmap(lisa_screen[lisa_screen_cur]);
    acquire_bitmap(screen);
    if (redraw_all) { 
    for (y=0;y<364;y++)
        for (x=0;x<90;x++)
        {
            mask=0x80;
            for (xx=0;xx<8;xx++)
            {
                if ((video[x+y*90]&mask)==mask) {
                   putpixel(lisa_screen[lisa_screen_cur],x*8+xx,y,black);
                   putpixel(lisa_screen[lisa_screen_next],x*8+xx,y,black);
                   }
                else {
                   putpixel(lisa_screen[lisa_screen_cur],x*8+xx,y,white);
                   putpixel(lisa_screen[lisa_screen_next],x*8+xx,y,white);
                }
                mask=mask>>1;
            }
        }
     }

//        blit(lisa_screen,screen,0,0,0,0,720,364);
          for (x=0;x<23;x++)
              for (y=0;y<12;y++) {
                  if (dirty_rect[x][y]>0) {
                  blit(lisa_screen[lisa_screen_cur],screen,x*32,y*32,x*32,y*32,32,32);
                    if (dirty_rect[x][y]>1)
                      blit(lisa_screen[lisa_screen_next],lisa_screen[lisa_screen_cur],x*32,y*32,x*32,y*32,32,32);
                    
                     dirty_rect[x][y]--;
                  }
              }
        release_bitmap(lisa_screen[lisa_screen_cur]);
        release_bitmap(screen);
        lisa_screen_cur=lisa_screen_next;
        lisa_screen_next=(lisa_screen_cur+1)&1;
}

int redraw_macxl(void)
{
    static int first=1;
    static int live=0;
    static BITMAP *lisa_screen=NULL;
    int y,x,xx;
    uint8* video=getVideoMemory();
    int mask;
    int black=makecol(0, 0, 0);
    int white=makecol(255, 255, 255);
    int red=makecol(255, 0, 0);
    if (first)
    {
         lisa_screen=create_bitmap(608,431);
         first=0;
    }
              
//    acquire_bitmap(lisa_screen);
    for (y=0;y<431;y++)
        for (x=0;x<76;x++)
        {
            mask=0x80;
            for (xx=0;xx<8;xx++)
            {
                if ((video[x+y*76]&mask)==mask)
                   putpixel(lisa_screen,x*8+xx,y,black);
                else
                   putpixel(lisa_screen,x*8+xx,y,white);
                mask=mask>>1;
            }
        }
        // add a "galactica" like live indicator
        putpixel(lisa_screen,live,431,red);
        live=(live+1)%608;
        blit(lisa_screen,screen,0,0,0,0,608,431);
}


void lisa_loop(uint32 targetPC)
{
     static int vblnum=0;
     int lastvbl=vbl;
     idle_time delta;
     int old_clock=0;
     int skip=3;

     int instr_cycles;
     int cycles=0;
     
     IDLE_INIT_FUNC("main_loop()");
     
     loop_exit=0;
     scare_mouse();
     clock68k=0;
     delta.seconds=0;
     delta.subseconds=55;
     ask_bkpt=0;
     redraw_lisa(1);

     do {
              getKeyEvent();
              for (screen_line=0;screen_line<364;screen_line++)
              {
               set_trace_time((vblnum<<16)|screen_line);
               // execute more or less 55 mem cycles
               while (cycles<180) 
               {
     if (((instr_cycles=m68k_execute(1))==-1)||(ask_bkpt)||(get_pc==targetPC)) goto end;
     PCDBG();
                  cycles+=((instr_cycles/4)+1)*4;
#ifdef _ACCURATE_TIMERS_
              delta.subseconds=((instr_cycles/4)+1)*4;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif
              }
#ifndef _ACCURATE_TIMERS_
              delta.subseconds=cycles;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif

              beginHBL();
               while (cycles<220) 
               {
     if (((instr_cycles=m68k_execute(1))==-1)||(ask_bkpt)||(get_pc==targetPC)) goto end;
     PCDBG();
              cycles+=((instr_cycles/4)+1)*4;
#ifdef _ACCURATE_TIMERS_
              delta.subseconds=((instr_cycles/4)+1)*4;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif
              }
#ifndef _ACCURATE_TIMERS_
              delta.subseconds=cycles;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif
              endHBL();
              
              cycles-=220;
              }
             if (beginVBL()) {
                  set_irq_line(VBLIRQ);
             }
             
              for (screen_line=364;screen_line<382;screen_line++)
              {
               set_trace_time((vblnum<<16)|screen_line);
               // execute more or less 55 mem cycles
               while (cycles<220) 
               {
     if (((instr_cycles=m68k_execute(1))==-1)||(ask_bkpt)||(get_pc==targetPC)) goto end;
     PCDBG();

              cycles+=((instr_cycles/4)+1)*4;
#ifdef _ACCURATE_TIMERS_
              delta.subseconds=((instr_cycles/4)+1)*4;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif
              }
#ifndef _ACCURATE_TIMERS_
              delta.subseconds=cycles;
              idle_timer_update_clock(delta);
              idle_check_timers();
#endif
              cycles-=220;
              
              }
                 if (endVBL())
                     reset_irq_line(VBLIRQ);
              
              {
             int l_mouse_x,l_mouse_y;
             // many thanx to allegro coders for this relative mouse routine ;-)
             get_mouse_mickeys(&l_mouse_x,&l_mouse_y);
             if ((l_mouse_x!=0) || (l_mouse_y!=0)) {
                if (l_mouse_x<-128) l_mouse_x=-128;
                if (l_mouse_x>127) l_mouse_x=127;
                if (l_mouse_y<-128) l_mouse_y=-128;
                if (l_mouse_y>127) l_mouse_y=127;
                
                via_keyb_update_mouse_pos((int8)l_mouse_x,(int8)l_mouse_y);
                }
                else
                    getMouseButton();
                // check for popup
                if ((mouse_b & 2)==2) {
                   show_mouse(mouse_sprite);
                   // waits for release...
                   while ((mouse_b&2)==2);
                   fastPopup();
                   show_mouse(NULL);
                }
                position_mouse(720/2,364/2);
             }
          
//             if ((vblnum&0x03)==0)
//              if (must_redraw())
                  redraw_lisa(0);
              vblnum++;
              
              if (exact) {
                            while (vbl<(lastvbl+10)) rest(0);
              }
              else {
                   if (vbl-lastvbl>0)
                   IDLE_DEBUG("Elapsed vbl=%d running=%d",
                                       vbl-lastvbl,
                                       1000/(vbl-lastvbl));
              }
              
              lastvbl=vbl;
              
          } while (!loop_exit);
      // wait for F9 release
      while (key[KEY_F9]);
end:
      unscare_mouse();
      ask_bkpt=0;    
      disass_debugger();
      regs_debugger();
      refresh_mmu_box();
      refresh_sharedram_box();
      get_log_str(log_box,sizeof(log_box_init));
}

int tobreak(void)
{
    uint32 targetPC;
    char str_alert[256];
    sscanf(breakpoint_str,"%x",&targetPC);
    sprintf(str_alert,"Go to %06x ?",targetPC);
   if (alert(str_alert, NULL, NULL, "&Yes", "&No", 'y', 'n') == 1)
   {
      int end=0;
      lisa_keyb();
      lisa_loop(targetPC);
      allegro_keyb();
      disass_debugger();
      regs_debugger();
      refresh_mmu_box();
    refresh_sharedram_box();
    }
    get_log_str(log_box,sizeof(log_box_init));
    return D_REDRAW;
}

int tobreaknoconfirm(void)
{
    uint32 targetPC;
    char str_alert[256];
    sscanf(breakpoint_str,"%x",&targetPC);
   {
      int end=0;
      lisa_keyb();
      lisa_loop(targetPC);
      allegro_keyb();
      disass_debugger();
      regs_debugger();
      refresh_mmu_box();
    refresh_sharedram_box();
    }
    get_log_str(log_box,sizeof(log_box_init));
    return D_REDRAW;
}


int info_address_mmu(void)
{
    uint32 targetPC;
    char *str_alert;
    sscanf(breakpoint_str,"%x",&targetPC);
    str_alert=get_address_info_mmu(targetPC);
   if (alert(str_alert, NULL, NULL, "&Yes", "&No", 'y', 'n') == 1)
   {
   }
    return D_REDRAW;
}

int info_address_phys(void)
{
    uint32 targetPC;
    char *str_alert;
    sscanf(breakpoint_str,"%x",&targetPC);
    str_alert=get_address_info_phys(targetPC);
   if (alert(str_alert, NULL, NULL, "&Yes", "&No", 'y', 'n') == 1)
   {
   }
    return D_REDRAW;
}

int preview(void)
{
    redraw_lisa(1);
    while (!key[KEY_SPACE]);
    return D_REDRAW;
}

int lisa_reset(void)
{
    mmuReset();

    m68k_init();
    m68k_pulse_reset();

      disass_debugger();
      regs_debugger();
    get_log_str(log_box,sizeof(log_box_init));
    return D_REDRAW;
}




int insert_dc42_image(int slot)
{
    char path[6*1024];
    int ret;
    path[0]='\0';
    file_select_ex("Insert a Disc copy 4.2 image",
                           path,
                           NULL,
                           1024,
                           480,
                           384);
    ret=dc42_insert(path,slot);
    if (ret!=0) alert("Bad DC42 image","disk unloaded" , NULL, "OK", NULL, 0, 0);
   
    return D_REDRAW;
}

int insert_dart_image(void)
{
    char path[6*1024];
    path[0]='\0';
    file_select_ex("Do not insert DART image (unfinished)",
                           path,
                           NULL,
                           1024,
                           480,
                           384);
    dart_insert(path,0);
        return D_REDRAW;
}

int call_memory_explorer_mmu(void)
{
    memory_explorer_mmu();
    return D_REDRAW;
}

int call_memory_explorer_raw(void)
{
    return D_REDRAW;
}

int clear_log(void)
{
    clear_log_str();
    return D_REDRAW;
}

/* the first menu in the menubar */
MENU commands[] =
{
   
   { "Step \tF7", step,  NULL,      0,  NULL  },
   { "Step over\tF8", stepover,  NULL,      0,  NULL  },
   { "Go to breakpoint", tobreak,  NULL,      0,  NULL  },
   { "screen preview", preview,  NULL,      0,  NULL  },
   { "&Quit debugger\tESC",        quit,  NULL,      0,  NULL  },
   { NULL,                   NULL,  NULL,      0,  NULL  }
};

/* the help menu */
MENU helpmenu[] =
{
   
   { "&About \tF1",      about,  NULL,      0,  NULL  },
   { NULL,               NULL,  NULL,      0,  NULL  }
};


/* the memory menu */
MENU memory_menu[] =
{
   
   { "MMU memory explorer",      call_memory_explorer_mmu,  NULL,      0,  NULL  },
   { "Raw memory explorer",      call_memory_explorer_raw,  NULL,      0,  NULL  },
   { NULL,               NULL,  NULL,      0,  NULL  }
};

/* the main menu-bar */
MENU the_menu[] =
{
   
   { "&Commands",  NULL,   commands,          0,      NULL  },
   { "Memory",  NULL,   memory_menu,          0,      NULL  },
   { "&Help",   NULL,   helpmenu,       0,      NULL  },
   { NULL,      NULL,   NULL,           0,      NULL  }
};

DIALOG the_dialog[] =
{
   /* (dialog proc)     (x)   (y)   (w)   (h) (fg)(bg) (key) (flags)     (d1) (d2)    (dp)                   (dp2) (dp3) */
   { d_clear_proc,        0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  },
   { d_menu_proc,         0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    the_menu,               NULL, NULL  },
   { d_text_proc,         0,  20,    0,    0,   0,  0,    0,      0,       0,   0,    (void *)"Lisa emulator integrated debugger",          NULL, NULL  },
   // a 60x20 char box for disass
   { d_textbox_proc,    0, 30,  480,   160,   0,  0,    0,      0,       0,   0,    (void *)disass_box,       NULL, NULL  },
   // a 60x20 char box for loging events
   { d_textbox_proc,    0, 200,  480,   152,   0,  0,    0,      0,       0,   0,    (void *)log_box,       NULL, NULL  },
   // a 30x20 char box for registers
   { d_textbox_proc,    488, 30,  232,   100,   0,  0,    0,      0,       0,   0,    (void *)registers_box,       NULL, NULL  },
   { d_textbox_proc,    488, 130,  232,   70,   0,  0,    0,      0,       0,   0,    (void *)sharedram_box,       NULL, NULL  },
   // the breakpoint
   { d_edit_proc,    488, 206,  232,   8,   0,  0,    0,      0,       6,   0,    (void *)breakpoint_str,       NULL, NULL  },
   // the mmu Entry
   { d_textbox_proc,    488, 222,  232,   130,   0,  0,    0,      0,       0,   0,    (void *)mmu_box,       NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_F7,   0,    (void *)step,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_F8,   0,    (void *)stepover,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_B,   0,    (void *)tobreaknoconfirm,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_R,   0,    (void *)lisa_reset,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_C,   0,    (void *)clear_log,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_M,   0,    (void *)info_address_mmu,          NULL, NULL  },
   { d_keyboard_proc,     0,   0,    0,    0,   0,  0,    0,      0,  KEY_P,   0,    (void *)info_address_phys,          NULL, NULL  },
   { d_yield_proc,        0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  },
   { NULL,                0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  }
};



void debugger(void)
{
     int i;
   /* set up colors */
   gui_fg_color = makecol(0, 0, 0);
   gui_mg_color = makecol(128, 128, 128);
   gui_bg_color = makecol(200, 200, 200);
   set_dialog_color (the_dialog, gui_fg_color, gui_bg_color);

   /* white color for d_clear_proc and the d_?text_procs */
   the_dialog[0].bg = makecol(255, 255, 255);
   for (i = 4; the_dialog[i].proc; i++)
      if (the_dialog[i].proc == d_text_proc ||
          the_dialog[i].proc == d_ctext_proc ||
          the_dialog[i].proc == d_rtext_proc)
         the_dialog[i].bg = the_dialog[0].bg;
   /* shift the dialog 2 pixels away from the border */
   position_dialog (the_dialog, 2, 2);

   breakpoint_str[0]='\0';
   mmu_box[0]='\0';
   disass_debugger();
   regs_debugger();
   /* do the dialog */
   do_dialog(the_dialog, -1);
}

int main() {
    char buf[256];
    int line;
    char *err;
    int ret;
    int revision;
    IDLE_INIT_FUNC("main()");
	init();
	IDLE_DEBUG("Init mem");
	
	ret=alert("Which model?", err, NULL, "Lisa1","Lisa2", '1', '2');
	
	if (ret==1) {
	    revision=0x40;
	} else {
	    revision=0xA8;
	}
    if ((err=MemInit(ret))!=NULL)
          if (alert("Memory initialization failed", err, NULL, "&Abort", NULL, 'a', 0) == 1)
          {
              exit (1);
          }

	IDLE_WARN("Init via1");
    init_via_keyb_io();
    if (check_profile_image()==-1) {
        if (alert("Profile not init should I create 5 or 10Mb",err,NULL,"&5 Mb","&10 Mb",'5','1')==1)
                init_via_hdd_io(0);
        else
                init_via_hdd_io(1);
    }
    else
        init_via_hdd_io(-1);
    init_floppy_io(revision); 
    scc_init();
    
    get_log_str(log_box,sizeof(log_box_init));


    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();


// go live at boot (bkpt 1 never reached (causes an address error...))
    lisa_keyb();
    lisa_loop(1);
    allegro_keyb();

    debugger();
	
    allegro_keyb();
	deinit();
	return 0;
}
END_OF_MAIN()

void init() {
	int depth, res;
	allegro_init();
	depth = desktop_color_depth();
	if (depth == 0) depth = 16;
	set_color_depth(depth);
#ifdef WIN32
	res = set_gfx_mode(GFX_DIRECTX_WIN, 720, 364, 0, 0);
#else
 	res = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 720, 364, 0, 0);
#endif
	if (res != 0) {
		allegro_message(allegro_error);
		exit(-1);
	}

	install_timer();
	install_keyboard();
	install_mouse();
    LOCK_FUNCTION(keypress_handler);
    LOCK_VARIABLE(last_scancode);
    LOCK_VARIABLE(vbl);
    LOCK_FUNCTION(vblupdate);
    // 60 frames per sec * 10
    install_int_ex(vblupdate,BPS_TO_TIMER(60*10));
}

void deinit() {
	clear_keybuf();
	/* add other deinitializations here */
}
