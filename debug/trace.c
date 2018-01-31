#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "../config.h"
#include "../cpu/m68k.h"


char *g_func_name="void";

#define LOG_LINE_SIZE	256
#define LOG_NB_LINES	1000

#define MAX_DEBUG_FUNC 100

static char debug_func_list[MAX_DEBUG_FUNC][64];
static int debug_func_list_size=0;
static int s_trace_time=0;
static int trace_init_flag=0;

char * trace_log_tab;
static int trace_log_next=0;
static int trace_log_size=0;

static FILE* log=NULL;

int g_notrace=0;

void
no_trace(void) {
   g_notrace=1;
}

void
restore_trace(void) {
   g_notrace=0;
}

void 
clear_log_str(void) {
   trace_log_next=0;
   trace_log_size=0;
}

int
init_trace(FILE * logStream) {
	trace_log_tab=(char *)malloc(LOG_LINE_SIZE*LOG_NB_LINES);
	memset(trace_log_tab,LOG_LINE_SIZE*LOG_NB_LINES,'\0');
	trace_init_flag=1;
	{
		FILE *f;
		char func[256];
		int err;
		if ((f=fopen("debug.lst","r"))!=NULL)
		{
			while (!feof(f)) {
				err=fscanf(f,"%s",func);
				if (err>0)
				{
					strncpy(debug_func_list[debug_func_list_size],func,63);
					debug_func_list[debug_func_list_size][63]='\0';
					debug_func_list_size++;
				}
			}
			fclose(f);
		}
	}
	if (logStream!=NULL) {
		log=logStream;
	}
}

int
deinit_trace(void) {
	if (trace_init_flag) free(trace_log_tab);
}

// utility func 
int 
check_debug(char *func_name) {
		int i;
		if (!trace_init_flag) init_trace(NULL);
		for (i=0;i<debug_func_list_size;i++)
		{
			// function is to debug
			if (strcmp(func_name,debug_func_list[i])==0) return 1;
		}
		// function not to trace
		return -1;
}

int
do_notrace(char *fmt,...) {
}

// helper function to format messages
int
do_trace(char *fmt,...) {
	char trace_out[4096];
	char trace_cur[4096];
	char fmt2[16];
	int i,j;
	char *t;
	va_list list;
	sprintf(trace_out,"%08X %06X ",s_trace_time,get_pc);
	strcat(trace_out,g_func_name);
	strcat(trace_out,":");
	va_start(list, fmt);
	for (i=0;fmt[i]!='\0';i++) {
		if (fmt[i]!='%') sprintf(trace_cur,"%c",fmt[i]);
		if (fmt[i]=='%') {
            int end=0;
            j=0;
            while ((!end) && (j<15)) {
                  fmt2[j]=fmt[i];
			      switch (fmt[i]) {
                         case '%' :
                         case '0' : case '1' : case '2' : case '3' : case '4' :
                         case '5' : case '6' : case '7' : case '8' : case '9' :
                         case '.' :
                         case '-' :
                         case 'l' :
                                     i++;
                                     break;
                         case 'd' :
                         case 'u' :
                         case 'x' :
                         case 'X' :
                         case 's' :
                         case 'c' :
                         case 'f' :
                                   end=1;
                         }
                  j++;
               }
               fmt2[j]='\0';
                                                  
			if (fmt[i]=='d') sprintf(trace_cur,fmt2,va_arg(list,int));
			if (fmt[i]=='u') sprintf(trace_cur,fmt2,va_arg(list,unsigned int));
			if (fmt[i]=='x') sprintf(trace_cur,fmt2,va_arg(list,int));
			if (fmt[i]=='X') sprintf(trace_cur,fmt2,va_arg(list,int));
			if (fmt[i]=='s') sprintf(trace_cur,fmt2,va_arg(list,char *));
			if (fmt[i]=='f') sprintf(trace_cur,fmt2,va_arg(list,double));
		}
		strcat(trace_out,trace_cur);
	}
	va_end(list);
	// update the log (cycle buffer)
	strncpy(&trace_log_tab[trace_log_next*LOG_LINE_SIZE],trace_out,LOG_LINE_SIZE);
	if (log!=NULL) {
		fprintf(log,trace_out);
		fprintf(log,"\n");
		fflush(log);
	}
	trace_log_tab[trace_log_next*LOG_LINE_SIZE+LOG_LINE_SIZE-1]='\0';
	trace_log_next=(trace_log_next+1)%LOG_NB_LINES;
	if (trace_log_size<LOG_NB_LINES) trace_log_size++;
	return 0;
}

// fills a buffer with traces (called by debugger)
void
get_log_str(char *str,int str_size) {
	char *str_where=str;
	int i;
	int line_to_display=0;
	int log_size=0;
	int cur_line=(trace_log_next-1)%LOG_NB_LINES;
	// first go backward to count lines to keep
	for (i=0;i<trace_log_size;i++) {
		if (2+log_size+strlen(&trace_log_tab[cur_line*LOG_LINE_SIZE])>str_size)
			break;
		log_size+=1+strlen(&trace_log_tab[cur_line*LOG_LINE_SIZE]);
		cur_line=(LOG_NB_LINES+cur_line-1)%LOG_NB_LINES;
		line_to_display++;
	}
	// then builds the log
	cur_line=(LOG_NB_LINES+trace_log_next-1-line_to_display)%LOG_NB_LINES;
	for (i=0;i<line_to_display;i++) {
		strcpy(str_where,&trace_log_tab[cur_line*LOG_LINE_SIZE]);
		str_where+=strlen(&trace_log_tab[cur_line*LOG_LINE_SIZE]);
		*str_where='\n';
		str_where++;
		cur_line=(cur_line+1)%LOG_NB_LINES;
	}
	str_where='\0';
}
void
set_trace_time(int a_time) {
   s_trace_time=a_time;
}
