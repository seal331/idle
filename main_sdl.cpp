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
   xx/09/2015   _ twiggy support
   24/09/2015   _ adding SDL1.x + hatari/aranym gui support (replaces allegro4)
 */

#include <SDL.h>
#include "sdlgui.h"

const SDL_VideoInfo* videoInfo;
SDL_Surface *screen;
static int g_isConsole=0;
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/m68k.h"

#include "mmu/mmu.h"
#include "cpu/debug.h"
#include "io/via.h"
#include "io/8530scc.h"
#include "disk/disk_image.h"
#include "disk/disk_ctrl.h"
#include "time/timer.h"
#include "irq/irq.h"

// #include "debug/mem_explorer.h"

int ask_bkpt=0;
static int exact=1;

// make local for gui to adapt
static int revision;

#define PC_DEBUG_SIZE 8
uint32 pc_debug[PC_DEBUG_SIZE];
int last_pc_debug=0;

#define PCDBG() pc_debug[last_pc_debug]=get_pc;last_pc_debug=((last_pc_debug+1)%PC_DEBUG_SIZE)

void init();
void deinit();

static  int insert_dc42_image(int slot);
static  int insert_dart_image(void);
static int lisa_reset(void);
int my_sdl_alert1(char *message,char *but1);
int my_sdl_alert2(char *message,char *but1,char *but2);

char debug_str[6*4+1];


int quit(void)
{
   if (my_sdl_alert2("Really Quit?", "Yes", "No") == 0)
      return 0;
   else
      return 1;
}


int reset_or_turnout(void)
{
   if (my_sdl_alert2("What to do?", "RESET", "Turn Off") == 0)
      return 0;
   else
      return 1;
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
                  uint32 l_pc=m68k_get_reg(NULL,M68K_REG_PC);
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
                      sprintf(addr,"%06lX ",l_pc);
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
          sprintf(buf,"[n-%d = x%06lx]",i,pc_debug[(last_pc_debug-i+PC_DEBUG_SIZE)%PC_DEBUG_SIZE]);
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
    return 0;
}

// loop CPU instruction until PC is greater (use this for loop)
int stepover(void)
{

                  uint32 savedPC=m68k_get_reg(NULL,M68K_REG_PC);

    do {
       log_box[0]='\0';
     m68k_execute(1);
       
    } while (((m68k_get_reg(NULL,M68K_REG_PC)<=savedPC) || (m68k_get_reg(NULL,M68K_REG_PC)>savedPC+8)));
 
    disass_debugger();
    regs_debugger();
    refresh_mmu_box();
    refresh_sharedram_box();
    get_log_str(log_box,sizeof(log_box_init));
    return 0;
}

static SGOBJ popupdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 36,16, NULL },
	{ SGBUTTON, 0, 0, 2,2, 32,1, "Disk image DC42 drive 1" },
	{ SGBUTTON, 0, 0, 2,4, 32,1, "Disk image DC42 drive 2" },
	{ SGBUTTON, 0, 0, 2,6, 32,1, "Call Debugger" },
	{ SGBUTTON, 0, 0, 2,8, 32,1, "Set options" },
	{ SGBUTTON, 0, 0, 2,10, 32,1, "RESET / TURN OFF" },
	{ SGBUTTON, 0, 0, 2,12, 32,1, "EXIT (should try turn off first)" },
	{ SGBUTTON, 0, 0, 2,14, 32,1, "Return" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};

/* 
 return 0 : continue emulation
 return 1 : exit program
 return 2 : exit to debug
*/
int
popup_menu() {
	int ret;
	
	if (revision==0xA8) {
		popupdlg[1].txt="<void>";
	}
	SDLGui_CenterDlg(popupdlg);
	ret = SDLGui_DoDialog(popupdlg, NULL);
	switch (ret) {
		case 1:
    		insert_dc42_image(0);
    		return 0;	
		    break;
    	case 2:
    		insert_dc42_image(1);
		    return 0;	
		    break;
    	case 3:
		    return 2;	
		    break;
    	case 4:
		    return 0;	
		    break;
    	case 5:
                 if   (reset_or_turnout()==0)
                      lisa_reset();
                 else
                     via_keyb_power_off();
		    return 0;	
		    break;
    	case 6:
                if (quit()==0) {
		    return 1;	
	}
		    break;
    	case 7:
		    return 0;	
		    break;
	}
	//printf("ret=%d\n",ret-1);
	return 0;
}

volatile int vbl;

void vblupdate(void)
{
 vbl++;
}

volatile int last_scancode;
      
void keypress_handler(int scancode)
{
     last_scancode=scancode;
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
	
int sdl2lisa_scancode(SDLKey key,unsigned char lisaMode)
{
    int lkey;
    IDLE_INIT_FUNC("sdl2lisa_scancode()");
    
    switch (key) {
           case SDLK_LCTRL : lkey=0x7F ;break;
           case SDLK_LALT : lkey=0x7F ;break;
           case SDLK_RALT : lkey=0x68 ;break;
           case SDLK_RETURN : lkey=0x48 ;break;
           case SDLK_BACKSPACE : lkey=0x45 ;break;
           case SDLK_LSHIFT : lkey=0x7E ;break;
           case SDLK_RSHIFT : lkey=0x7E ;break;
           case SDLK_TAB : lkey=0x78 ;break;


           case SDLK_1 : lkey=0x74 ;break;
           case SDLK_2 : lkey=0x71 ;break;
           case SDLK_3 : lkey=0x72 ;break;
           case SDLK_4 : lkey=0x73 ;break;
           case SDLK_5 : lkey=0x64 ;break;
           case SDLK_6 : lkey=0x61 ;break;
           case SDLK_7 : lkey=0x62 ;break;
           case SDLK_8 : lkey=0x63 ;break;
           case SDLK_9 : lkey=0x50 ;break;
           case SDLK_0 : lkey=0x51 ;break;
           case SDLK_MINUS : lkey=0x40 ;break; 
           case SDLK_EQUALS : lkey=0x41 ;break;
           
           case SDLK_q : lkey=0x75 ;break;
           case SDLK_w : lkey=0x77 ;break;
           case SDLK_e : lkey=0x60 ;break;
           case SDLK_r : lkey=0x65 ;break;
           case SDLK_t : lkey=0x66 ;break;
           case SDLK_y : lkey=0x67 ;break;
           case SDLK_u : lkey=0x52 ;break;
           case SDLK_i : lkey=0x53 ;break;
           case SDLK_o : lkey=0x5f ;break;
           case SDLK_p : lkey=0x44 ;break;
           
           case SDLK_a : lkey=0x70 ;break;
           case SDLK_s : lkey=0x76 ;break;
           case SDLK_d : lkey=0x7b ;break;
           case SDLK_f : lkey=0x69 ;break;
           case SDLK_g : lkey=0x6a ;break;
           case SDLK_h : lkey=0x6b ;break;
           case SDLK_j : lkey=0x54 ;break;
           case SDLK_k : lkey=0x55 ;break;
           case SDLK_l : lkey=0x59 ;break;

           case SDLK_z : lkey=0x79 ;break;
           case SDLK_x : lkey=0x7a ;break;
           case SDLK_c : lkey=0x6d ;break;
           case SDLK_v : lkey=0x6c ;break;
           case SDLK_b : lkey=0x6e ;break;
           case SDLK_n : lkey=0x6f ;break;
           case SDLK_m : lkey=0x58 ;break;

           case SDLK_SLASH : lkey=0x4C ;break;
           case SDLK_COMMA : lkey=0x5D ;break;
//           case SDLK_STOP   : lkey=0x5E ;break;

           case SDLK_LEFTBRACKET : lkey=0x56 ;break;
           case SDLK_RIGHTBRACKET   : lkey=0x57 ;break;
           

           case SDLK_SEMICOLON :     
           case SDLK_COLON : lkey=0x5A ;break;
           case SDLK_QUOTE   : lkey=0x5B ;break;
           
//           case SDLK_TILDE : lkey=0x68 ;break;
           case SDLK_BACKSLASH : lkey=0x42;break;
           
           case SDLK_F12 : if (lisaMode)
                             m68k_set_irq(M68K_IRQ_7);
                           else
                             m68k_set_irq(M68K_IRQ_NONE);
                          break;
           case SDLK_F10: if (lisaMode)
                              via_keyb_power_off();
                         break;
           case SDLK_F9 : if (lisaMode) {
                             IDLE_TRACE("Interrupt 6");
                             m68k_set_irq(M68K_IRQ_6);
                             }
                           else
                             m68k_set_irq(M68K_IRQ_NONE);
                           lkey=0x5c;
                          break;
           
           case SDLK_F1 : exact=0;
                       lkey=0x5c;
                       break;
           case SDLK_F2 : exact=1;
                         lkey=0x5c;
                        break;

           case SDLK_F3 :lkey=0x48;
                        break;

           case SDLK_F4 :lkey=0x4E;
                        break;

           case SDLK_F5 :lkey=0x7C;
                        break;

           case SDLK_F8 : loop_exit=1;
           // under allegro this will cause program end
           // case KEY_ESC : loop_exit=1;
           
           default : lkey=0x5c; // SPACE by default
                     IDLE_TRACE("Pressed unmapped 0x%x",key);
    }
    IDLE_TRACE("Pressed mapped %x %x",key,lkey);
   
    return (lkey|lisaMode);
}


void				   
handleSdlKeyEvent(SDL_Event *event) {
    if (event->type==SDL_KEYDOWN) 
        via_keyb_event_key(sdl2lisa_scancode(event->key.keysym.sym,0x80));
    else
        via_keyb_event_key(sdl2lisa_scancode(event->key.keysym.sym,0x00));
}


/*
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
*/

static inline void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

static int dirty_rect[23][12];
static SDL_Surface *lisa_screen[2];
static int lisa_screen_cur=0;
static int lisa_screen_next=1;
static int screen_line;

static int black,white; // should be in lisa screen color space

#ifdef __cplusplus
extern "C" {
#endif

void redraw_lisa_byte(uint32 video_offset);     
void redraw_lisa_byte(uint32 video_offset) {
     int y,x,xx;
     uint8* video=getVideoMemory();
     int mask;
     
     x=video_offset%90;
     y=video_offset/90;
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
}
#ifdef __cplusplus
}
#endif

int redraw_lisa(int redraw_all)
{
    static int first=1;
    static int live=0;
	SDL_Rect rect;
 
    int y,x,xx;
    uint8* video=getVideoMemory();
    int mask;
    if (first)
    {
         lisa_screen[0]=SDL_CreateRGBSurface(0,720,364,32,0,0,0,0);
         lisa_screen[1]=SDL_CreateRGBSurface(0,720,364,32,0,0,0,0);
	     black=SDL_MapRGB(lisa_screen[0]->format, 0x00, 0x00, 0x00);
	     white=SDL_MapRGB(lisa_screen[0]->format, 0xff, 0xff, 0xff);
         first=0;
    }
              
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
          for (x=0;x<23;x++)
              for (y=0;y<12;y++) {
                dirty_rect[x][y]=1;
              }
     }

          for (x=0;x<23;x++)
              for (y=0;y<12;y++) {
                  if (dirty_rect[x][y]>0) {
					rect.x=x*32;
					rect.y=y*32;
					rect.w=32;
					rect.h=32;
					SDL_BlitSurface(lisa_screen[lisa_screen_cur],&rect,screen,&rect);
                    if (dirty_rect[x][y]>1)
						SDL_BlitSurface(lisa_screen[lisa_screen_next],&rect,lisa_screen[lisa_screen_cur],&rect);
                    
                     dirty_rect[x][y]--;
                  }
              }
        lisa_screen_cur=lisa_screen_next;
        lisa_screen_next=(lisa_screen_cur+1)&1;
}
void
toggleFullScreen(void) {
    static int full=0;
    if (full==0) {
        screen = SDL_SetVideoMode(720, 364, 0, SDL_SWSURFACE|SDL_ANYFORMAT|SDL_FULLSCREEN);
        if (screen==NULL) {
            screen = SDL_SetVideoMode(720, 364, 0, SDL_SWSURFACE|SDL_ANYFORMAT);
        }
	    SDLGui_SetScreen(screen);
	    full=1;
    } else {
        screen = SDL_SetVideoMode(720, 364, 0, SDL_SWSURFACE|SDL_ANYFORMAT);
        if (screen==NULL) {
            SDL_Quit();
            exit (1);
        }
	    SDLGui_SetScreen(screen);
	    full=0;
    }
}

void
unscare_mouse(void) {
SDL_WM_GrabInput( SDL_GRAB_OFF );
SDL_ShowCursor(SDL_ENABLE);
}

void
scare_mouse(void) {
SDL_WM_GrabInput( SDL_GRAB_ON );
SDL_ShowCursor(SDL_DISABLE);
}

#define TICK_INTERVAL    16

static Uint32 next_time;

Uint32 time_left(void)
{
    Uint32 now;

    now = SDL_GetTicks();
    if(next_time <= now)
        return 0;
    else
        return (next_time - now);
}

int 
lisa_loop(uint32 targetPC)
{
     static int vblnum=0;
     int lastvbl=vbl;
     idle_time delta;
     int old_clock=0;
     int skip=3;
     int l_mouse_x,l_mouse_y;

     int instr_cycles;
     int cycles=0;
	 SDL_Event event;
         
     IDLE_INIT_FUNC("main_loop()");
     
     loop_exit=0;
     scare_mouse();
     clock68k=0;
     delta.seconds=0;
     delta.subseconds=55;
     ask_bkpt=0;
     redraw_lisa(1);

     next_time = SDL_GetTicks() + TICK_INTERVAL;

     while (1) {
		while ( SDL_PollEvent(&event) )
		{
			switch (event.type) {
				case SDL_QUIT:
					return 1;
				case SDL_KEYDOWN:
				    if (event.key.keysym.sym==SDLK_F11) {
				        toggleFullScreen();
				        redraw_lisa(1);
				        break;
				    }
				    handleSdlKeyEvent(&event);
				    break;
				case SDL_KEYUP:
				    handleSdlKeyEvent(&event);
				    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        unscare_mouse();
                        return 0;
                    }
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        via_keyb_event_key(0x86);
					}
					break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        via_keyb_event_key(0x06);
					}
					break;
				case SDL_MOUSEMOTION:
				    l_mouse_x=event.motion.xrel;
				    l_mouse_y=event.motion.yrel;
                    if ((l_mouse_x!=0) || (l_mouse_y!=0)) {
                        if (l_mouse_x<-128) l_mouse_x=-128;
                        if (l_mouse_x>127) l_mouse_x=127;
                        if (l_mouse_y<-128) l_mouse_y=-128;
                        if (l_mouse_y>127) l_mouse_y=127;
                
                        via_keyb_update_mouse_pos((int8)l_mouse_x,(int8)l_mouse_y);
                    }
				    break;	
			}	
		}		 
              // getKeyEvent();
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
              
          
                  redraw_lisa(0);
              vblnum++;
              
              lastvbl=vbl;
				SDL_Flip(screen);
			SDL_Delay(time_left());
			next_time += TICK_INTERVAL/((exact==1)?1:4);
          } // while (!loop_exit);
          
end:
      unscare_mouse();
      ask_bkpt=0;    
      // disass_debugger();
      // regs_debugger();
      // refresh_mmu_box();
      // refresh_sharedram_box();
      // get_log_str(log_box,sizeof(log_box_init));
}


int lisa_reset(void)
{
    mmuReset();

    m68k_init();
    m68k_pulse_reset();

      disass_debugger();
      regs_debugger();
    get_log_str(log_box,sizeof(log_box_init));
    return 1;
}


int insert_dc42_image(int slot)
{
    char path[6*1024];
    char* pt_path=path;
    int ret;
    char *retPath;

    IDLE_INIT_FUNC("insert_dc42_image()");

    path[0]='\0';
    
    retPath=SDLGui_FileSelect(pt_path,NULL,0);
                           
    IDLE_TRACE("File name :<%s>",retPath);
                           
    ret=dc42_insert(retPath,slot);
    if (ret!=0) my_sdl_alert1("Bad DC42 image disk" ,"OK");
   
    return 0;
}

static SGOBJ alertdlg1[] =
{
	{ SGBOX, 0, 0, 0,0, 25,6, NULL },
	{ SGTEXT, 0, 0, 2,1, 16,1, "This is a question" },
	{ SGBUTTON, 0, 0, 2,4, 13,1, "reply 1" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


int
my_sdl_alert1(char *message,char *but1) {
	int ret;
	alertdlg1[1].txt=message;
	alertdlg1[1].w=strlen(message);
	alertdlg1[2].txt=but1;

	
	SDLGui_CenterDlg(alertdlg1);
	ret = SDLGui_DoDialog(alertdlg1, NULL);
	// printf("ret=%d\n",ret-2);
	return ret-2;
}


static SGOBJ alertdlg2[] =
{
	{ SGBOX, 0, 0, 0,0, 30,8, NULL },
	{ SGTEXT, 0, 0, 2,1, 16,1, "This is a question" },
	{ SGBUTTON, 0, 0, 2,4, 13,1, "reply 1" },
	{ SGBUTTON, 0, 0, 2,6, 13,1, "reply 2" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


int
my_sdl_alert2(char *message,char *but1,char *but2) {
	int ret;
	alertdlg2[1].txt=message;
	alertdlg2[1].w=strlen(message);
	alertdlg2[2].txt=but1;
	alertdlg2[3].txt=but2;
	
	SDLGui_CenterDlg(alertdlg2);
	ret = SDLGui_DoDialog(alertdlg2, NULL);
	// printf("ret=%d\n",ret-2);
	return ret-2;
}


int main(int argc,char ** argv) {
    char buf[256];
    int line;
    char *err;
    int ret;

    int model;
    int doQuit;
    int argnum;
    IDLE_INIT_FUNC("main()");
	// init();

    for (argnum=1;argnum<argc;argnum++) {
        if (strcmp(argv[argnum],"-console")==0) {
            g_isConsole=1;
        }
    }
    

    /* Initialize defaults, Video and Audio */
    if((SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==-1)) { 
        printf("Could not initialize SDL: %s.\n", SDL_GetError());
        exit(-1);
    }	

    videoInfo = SDL_GetVideoInfo ();

    if (g_isConsole) {
        screen = SDL_SetVideoMode(videoInfo->current_w, videoInfo->current_h, videoInfo->vfmt->BitsPerPixel, SDL_SWSURFACE|SDL_ANYFORMAT);
    } else {
        screen = SDL_SetVideoMode(720, 364, 0, SDL_SWSURFACE|SDL_ANYFORMAT);
    }
    if ( screen == NULL ) {
        fprintf(stderr, "Couldn't set video mode: %s\n",
                        SDL_GetError());
        exit(1);
    }
    SDL_EnableUNICODE(true);

	SDLGui_Init();
	SDLGui_SetScreen(screen);
	SDL_ShowCursor(SDL_QUERY);
	SDL_ShowCursor(SDL_ENABLE);
        
	ret=my_sdl_alert2("Which model?","Lisa1","Lisa2");
	
	if (ret==0) {
		// lisa 1
	    revision=0x40;
	    model=1;
	} else {
		// lisa 2
	    revision=0xA8;
	    model=0;
	}
	
    if ((err=MemInit(model))!=NULL) {
        my_sdl_alert1(err,"Quit");
        SDL_Quit();
        exit (1);
    }

	IDLE_WARN("Init via1");
    init_via_keyb_io();
    if (check_profile_image()==-1) {
        if (my_sdl_alert2("Profile not init should I create 5 or 10Mb","5 Mb","10 Mb")==0)
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

    doQuit=0;
    while (doQuit==0) {
        if (lisa_loop(1)==1) {
            doQuit=1;
            break;
        }
    	if (popup_menu()==1) {
    	    doQuit=1;
    	}
    }
    SDL_Quit();
	deinit();
	return 0;
}


void deinit() {
//	clear_keybuf();
	/* add other deinitializations here */
}

//======================================================================================
// SDL debugger




