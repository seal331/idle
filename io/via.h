
// generic via define
#define VIA_ORB       0
#define VIA_ORA       1
#define VIA_DDRB      2
#define VIA_DDRA      3
#define VIA_T1C_L     4
#define VIA_T1C_H     5
#define VIA_T1L_L     6
#define VIA_T1L_H     7
#define VIA_T2C_L     8
#define VIA_T2C_H     9
#define VIA_SR        10
#define VIA_ACR       11
#define VIA_PCR       12
#define VIA_IFR       13
#define VIA_IER       14
#define VIA_ORA_DIRECT  15

// functions from via.c
#define IRQ_SET     0
#define IRQ_RESET   1
void via_init (int which, int tag_t1, int tag_t2,void (*irq_func)(int));
void via_reset(void);
void via_set_clock(int which,int clock);
int via_read(int which, int offset);
void via_write(int which, int offset, int data);
void via_set_input_a(int which, int data);
void via_set_input_b(int which, int data);
void via_set_input_ca1(int which, int data);
void via_set_input_ca2(int which, int data);
void via_set_input_cb1(int which, int data);
void via_set_input_cb2(int which, int data);
uint8 via_get_output_a(int which);
uint8 via_get_output_b(int which);
void
via_set_output_callback(int which,void (*changed_out_a)(int),void (*changed_out_b)(int));
void
via_set_stream_a_callback(int which,int (*stream_a)(int));

#define HDD_VIA 0
#define KEYB_VIA 1

void init_via_keyb_io(void);
int check_profile_image(void);
void init_via_hdd_io(int);
int via_keyb_event_key(uint8 code);
void via_keyb_update_mouse_pos(uint8 x, uint8 y);
void via_keyb_update_mouse_button(uint8 value);
void via_keyb_power_off(void);

void via_set_FDIR(void);
void via_reset_FDIR(void);


void via_set_DIAG(void);
void via_reset_DIAG(void);

void via_reset_PROFILE(int dummy);

void via_turn_on_PROFILE(void);
void via_turn_off_PROFILE(void);

