#include "stdarg.h"
#include "../config.h"

#ifdef _NOLOG_
#define IDLE_INIT_FUNC(A) 
#define IDLE_ERROR do_notrace
#define IDLE_WARN do_notrace
#define IDLE_TRACE do_notrace
#define IDLE_DEBUG do_notrace
#else
#define IDLE_INIT_FUNC(A) static int debug_flag=0;char * l_func_name=g_func_name=A;if ((!g_notrace) && (debug_flag==0)) debug_flag=check_debug(A)
#define IDLE_ERROR if (!g_notrace && ((g_func_name=l_func_name)!=NULL)) do_trace
#define IDLE_WARN if (!g_notrace && ((g_func_name=l_func_name)!=NULL)) do_trace
#define IDLE_TRACE if (!g_notrace && ((g_func_name=l_func_name)!=NULL)) do_trace
#define IDLE_DEBUG if ((!g_notrace && ((g_func_name=l_func_name)!=NULL)) && (debug_flag==1)) do_trace
#endif

// func name passed to trace
extern char *g_func_name;
extern int g_notrace;

void
no_trace(void);
void
restore_trace(void);
int
init_trace(void);
void 
clear_log_str(void);
int
deinit_trace(void);
int 
check_debug(char *func_name); 
int
do_notrace(char *fmt,...);
int
do_trace(char *fmt,...);
void
get_log_str(char *str,int str_size);
void
set_trace_time(int);
