CC=gcc
CFLAGS=-I.
DEPS = debug.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)


mpw_obj2asm: mpw_obj2asm.o debug.o 
	gcc -o mpw_obj2asm *.o -I.
