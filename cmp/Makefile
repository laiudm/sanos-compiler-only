#
# Makefile for cmp
#

all: cmp.exe

cmp.exe: cmp.c
	$(CC) -o cmp.exe cmp.c -DUSE_LOCAL_HEAP

	
install: cmp.exe
    cp cmp.exe ../bin/cmp.exe

clean:
    rm cmp.exe

