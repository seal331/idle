/*
 * Generic timer module
 * simplified MAME-like one shot timers 
 */

#include <stdlib.h>

#include "../debug/trace.h"

#include "timer.h"

// timers array
idle_timer timer_tab[MAX_TIMER];
int g_timer_num=0;

// global emulator time
idle_time g_timer_clock={0,0};

static idle_time idle_timer_add(idle_time t1,idle_time t2) {
	idle_time ret;
	ret.subseconds=t1.subseconds+t2.subseconds;
	ret.seconds=t1.seconds+t2.seconds;
	if (ret.subseconds>TIMER_FREQ) {
		ret.subseconds-=TIMER_FREQ;
		ret.seconds++;
	}
	return ret;
}

static int idle_timer_cmp(idle_time t1,idle_time t2) {
    if (t1.seconds>t2.seconds) return 1;
    if (t1.seconds<t2.seconds) return -1;
    if (t1.subseconds>t2.subseconds) return 1;
    if (t1.subseconds<t2.subseconds) return -1;
    return 0;
}

int idle_timer_create(	void (*callback)(int), 
						int callback_param, 
						int tag, 
						idle_time delta) {
	IDLE_INIT_FUNC("idle_timer_create()");
	if (g_timer_num>=MAX_TIMER) {
		IDLE_ERROR("Cannot allocate timer !!!");
		return 1;
	}
	IDLE_DEBUG("Create timer %d",tag);
	timer_tab[g_timer_num].callback=callback;
	timer_tab[g_timer_num].callback_param=callback_param;
	timer_tab[g_timer_num].tag=tag;
	timer_tab[g_timer_num].start=g_timer_clock;
	timer_tab[g_timer_num].expire=idle_timer_add(g_timer_clock,delta);	
	g_timer_num++;
}

void idle_timer_update_clock(idle_time delta) {
	g_timer_clock=idle_timer_add(g_timer_clock,delta);
}

void idle_check_timers(void) {
	int i;
	void (* callback)(int) ;
	int param;
	IDLE_INIT_FUNC("idle_check_timers()");
	for (i=0;i<g_timer_num;i++) {
		if (idle_timer_cmp(g_timer_clock,timer_tab[i].expire)>0) {
			IDLE_DEBUG("Timer %d elapsed",timer_tab[i].tag);
            // memorize callback
			callback=timer_tab[i].callback;
            param=timer_tab[i].callback_param;
			// then fills the missing slot
			if ((g_timer_num>1) && (i<g_timer_num-1)) {
               timer_tab[i]=timer_tab[g_timer_num-1];
            }
            g_timer_num--;
            // then call the callback (it may rearm itself...)
            (*callback)(param);
			return; // return here to avoid side effects
		}
	}
}

// removes timers of same tag
int idle_timer_remove(int tag) {
	int i,num=0;
	IDLE_INIT_FUNC("idle_timer_remove()");
	for (i=0;i<g_timer_num;i++) {
		if (timer_tab[i].tag==tag) {
			if ((g_timer_num>1) && (i<g_timer_num-1)) {
				timer_tab[i]=timer_tab[g_timer_num-1];
			}
			num++;
            g_timer_num--;
		}
	}
	if (num>0) IDLE_DEBUG("removed %d timers tag 0x%x",num,tag);
	return num;
}

double idle_timer_get_elapsed_time(int tag) {
       int i;
       double cur,start;
       IDLE_INIT_FUNC("idle_timer_get_elapsed_time()");
	   for (i=0;i<g_timer_num;i++) 
	       	if (timer_tab[i].tag==tag) break;
       if (i==g_timer_num) return 0.0;
       cur=g_timer_clock.seconds+(double)g_timer_clock.subseconds/(double)TIMER_FREQ;
       start=timer_tab[i].start.seconds+(double)timer_tab[i].start.subseconds/(double)TIMER_FREQ;
       return cur-start;
}

double idle_timer_get_left_time(int tag) {
       int i;
       double cur,expire,left;
       IDLE_INIT_FUNC("idle_timer_get_left_time()");
	   for (i=0;i<g_timer_num;i++) 
	       	if (timer_tab[i].tag==tag) break;
       if (i==g_timer_num) return 0.0;
       cur=g_timer_clock.seconds+(double)g_timer_clock.subseconds/(double)TIMER_FREQ;
       expire=timer_tab[i].expire.seconds+(double)timer_tab[i].expire.subseconds/(double)TIMER_FREQ;
       left=expire-cur;
       if (left < 0.0) return 0.0;
       return left;
}


idle_time idle_get_time(void) {
          return g_timer_clock;
}

double idle_get_time_double(void) {
       double val=g_timer_clock.seconds+(double)g_timer_clock.subseconds/(double)TIMER_FREQ;
       return val;
}

idle_time double2time(double t) {
          idle_time ret;
          IDLE_INIT_FUNC("double2time");
          ret.seconds=(unsigned int)t;
          ret.subseconds=(unsigned int)((t-(double)ret.seconds)*(double)TIMER_FREQ);
          IDLE_DEBUG("double %f = %d %d",t,ret.seconds,ret.subseconds);
          return ret;          
}
