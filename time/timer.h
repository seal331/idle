/*
 * Generic timer module
 * simplified MAME-like one shot timers 
 */
 
#define MAX_TIMER 32

// time frequency (main cpu clock) 
// must be less than MAXINT/2
#define TIMER_FREQ 5000000

typedef struct
{
	unsigned int	seconds;
	unsigned int	subseconds; // 0->TIMER_FREQ
} idle_time;


struct _idle_timer {
	int tag;	// caller must provide a unique tag to access one timer
	void (*callback)(int);
	int callback_param; // generaly a state indicator
	idle_time start;
	idle_time expire;	
};

typedef struct _idle_timer idle_timer;

int idle_timer_create(	void (*callback)(int), 
						int callback_param, 
						int tag, 
						idle_time delta);

// remove timer(s) based on tag
int idle_timer_remove(int tag); 

double idle_timer_get_elapsed_time(int tag);
double idle_timer_get_left_time(int tag);

// the global time update func
void idle_timer_update_clock(idle_time delta);

// returns after first timer elapsed
void idle_check_timers(void);

idle_time idle_get_time(void);
double idle_get_time_double(void);

idle_time double2time(double t);
