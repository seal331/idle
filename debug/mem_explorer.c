#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _CAST_
#include "../cpu/68000.h"
#else
#include "../cpu/m68k.h"
#endif

#include "../mmu/mmu.h"
#include "../io/via.h"

#define DUMP_BOX_SIZE 0x10000

extern DIALOG mmu_explorer_dial[];
#define SEL_NUM 5

char dump_box[DUMP_BOX_SIZE];
char start_str[6*4+1];
char last_str[6*4+1]="";
char last_sel[3]="";

void
fill_dump_box(void) {
    uint32 address;
    uint32 address1;
    uint32 start_address;
    int dump_size=0;
    int end=0;
    char *dump_ptr=dump_box;
    char tmp_str[6+1+5*8+1+16+1+1];
    char tmp2[16];
    char tmp3[16+1];
    char tmp_old[6+1+5*8+1+16+1+1];
    int same=0;
    unsigned char c;
    uint16 val;
    int i;
    tmp_old[0]='\0';
    tmp_old[7]='\0';
    
    no_trace(); // to avoid tracing of MMU errors...
    if (strcmp(last_str,start_str)!=0) {
        sscanf(start_str,"%x",&address);
        strcpy(last_str,start_str);
    }
    else {
         address=(mmu_explorer_dial[SEL_NUM].d1)*0x20000;
    }
    start_address=address;
    while (!end) {
          sprintf(tmp_str,"%06x ",address);
          for (i=0;i<8;i++) {
              sprintf(tmp2,"%04x ",val=LisaGetWDebug(address));
              address+=2;
              strcat(tmp_str,tmp2);
              // do the ASCII dump
              c=(val&0xFF00)>>8;         
              if ((c<32) || (c>127))
                 tmp3[i*2]='.';
              else
                  tmp3[i*2]=c;  
              c=(val&0xFF);         
              if ((c<32) || (c>127))
                 tmp3[i*2+1]='.';
              else
                  tmp3[i*2+1]=c;  
          }
          tmp3[16]='\0';
          strcat(tmp_str,tmp3);
          strcat(tmp_str,"\n");
          if (strcmp(&tmp_old[7],&tmp_str[7])==0) {
             if (same==0) {
                same=1;
                strcpy(tmp_old,tmp_str);
                strcpy(tmp_str,"*\n");
             }     
             else
                 same=2;
          }
          else {
               same=0;
               strcpy(tmp_old,tmp_str);
          }
          
          if (same!=2) {
             if (dump_size+strlen(tmp_str)+1>DUMP_BOX_SIZE) {
                end=1;
             }
             else {
                strcpy(dump_ptr,tmp_str);
                dump_ptr+=strlen(tmp_str);
                dump_size+=strlen(tmp_str);
             }    
          }
          if (address>0xFFFFFF) end=1;
          if (address-start_address>0x20000) end=1;
     }
     restore_trace();
}


/* callback function to specify the contents of the listbox */
char *listbox_seg_getter(int index, int *list_size)
{
   int i;
   static char segs[128][3]; 
   static int first=1;

   if (first) {
      first=0;
      for (i=0;i<128;i++)
          sprintf(segs[i],"%02x",i*2);
   }

   if (index < 0) {
      *list_size = 128;
      return NULL;
   }
   else
      return segs[index]; 
}

static int quit(void) {
  return D_CLOSE;
}

static int redraw(void) {
  fill_dump_box();
  return D_REDRAW;
}

static int dump_to_disk(void) {
    uint32 address;
    uint32 address1;
    uint32 start_address;
    int end,val;
    static int seqnum=0; 
    char fnm[256];
    FILE *f;
    
    sprintf(fnm,"FILE%03d.bin",seqnum);
    seqnum++;
   
    f=fopen(fnm,"wb");   

    no_trace(); // to avoid tracing of MMU errors...
    if (strcmp(last_str,start_str)!=0) {
        sscanf(start_str,"%x",&address);
        strcpy(last_str,start_str);
    }
    else {
         address=(mmu_explorer_dial[SEL_NUM].d1)*0x20000;
    }
    start_address=address;

    end=0;

    while (!end) {
          val=LisaGetWDebug(address);
          fputc((val>>8),f);
          fputc(val&0xFF,f);
          address+=2;
          if (address>0xFFFFFF) end=1;
          if (address-start_address>0x20000) end=1;
     }


    restore_trace();

    fclose(f);

  return D_REDRAW;
}

static MENU commands[] =
{
   
   { "redraw", redraw,  NULL,      0,  NULL  },
   {"Dump to disk",dump_to_disk, NULL, 0, NULL},
   { "Quit",   quit,  NULL,      0,  NULL  },
   { NULL,                   NULL,  NULL,      0,  NULL  }
};


static MENU memory_menu[] =
{
   
   { "command",  NULL,   commands,          0,      NULL  },
   { NULL,      NULL,   NULL,           0,      NULL  }
};

DIALOG mmu_explorer_dial[] =
{
   /* (dialog proc)     (x)   (y)   (w)   (h) (fg)(bg) (key) (flags)     (d1) (d2)    (dp)                   (dp2) (dp3) */
   { d_clear_proc,        0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  },
   { d_menu_proc,         0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    memory_menu,               NULL, NULL  },
   { d_text_proc,         0,  20,    0,    0,   0,  0,    0,      0,       0,   0,    (void *)"IDLE Memory explorer",          NULL, NULL  },
   // the start
   { d_text_proc,         0,  30,    0,    0,   0,  0,    0,      0,       0,   0,    (void *)"Start Address:",          NULL, NULL  },
   { d_edit_proc,         112,30,   56,    8,   0,  0,    0,      0,       6,   0,    (void *)start_str,       NULL, NULL  },
   // the segment selector
   { d_list_proc,       320, 0,  48,   38,   0,  0,    0,      0,       0,   0,    (void *)listbox_seg_getter, NULL,  NULL  },
   { d_button_proc,     360,  0,  80,   38,   0,  0,  'r',      0,       0,   0,    (void *)"Redraw",          NULL, NULL  },
   // a 60x20 char box for memory dump
   { d_textbox_proc,    0, 40,  700,   310,   0,  0,    0,      0,       0,   0,    (void *)dump_box,       NULL, NULL  },
   { d_yield_proc,        0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  },
   { NULL,                0,   0,    0,    0,   0,  0,    0,      0,       0,   0,    NULL,                   NULL, NULL  }
};


void
memory_explorer_mmu(void) {
     int i;
   /* set up colors */
   gui_fg_color = makecol(0, 0, 0);
   gui_mg_color = makecol(128, 200, 128);
   gui_bg_color = makecol(200, 255, 200);
   set_dialog_color (mmu_explorer_dial, gui_fg_color, gui_bg_color);

   /* white color for d_clear_proc and the d_?text_procs */
   mmu_explorer_dial[0].bg = makecol(255, 255, 255);
   for (i = 4; mmu_explorer_dial[i].proc; i++)
      if (mmu_explorer_dial[i].proc == d_text_proc ||
          mmu_explorer_dial[i].proc == d_ctext_proc ||
          mmu_explorer_dial[i].proc == d_rtext_proc)
         mmu_explorer_dial[i].bg = mmu_explorer_dial[0].bg;
   /* shift the dialog 2 pixels away from the border */
   position_dialog (mmu_explorer_dial, 2, 2);

   start_str[0]='0';
   start_str[1]='\0';
   dump_box[0]='\0';
   fill_dump_box();
   /* do the dialog */
   do_dialog(mmu_explorer_dial, -1);
}
