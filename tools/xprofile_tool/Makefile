CC=gcc
CFLAGS=-I.

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)


xprofile_tool: xprofile_tool.o 
	gcc -o xprofile_tool *.o -I.
