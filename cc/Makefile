#
# Makefile for C compiler
#

all: cc.exe

TCC_SRCFILES=asm386.c asm.c cc.c codegen386.c codegen.c compiler.c elf.c pe.c preproc.c symbol.c type.c util.c
TCC_HDRFILES=cc.h config.h elf.h opcodes.h tokens.h

cc.exe: $(TCC_SRCFILES) $(TCC_HDRFILES)
    $(CC) -o cc.exe -m mapfile $(TCC_SRCFILES) -DUSE_LOCAL_HEAP -noshare

install: cc.exe
    cp cc.exe $(ROOT)/usr/bin/cc.exe

cross: install

clean:
    rm cc.exe

compile:
	echo compiler = $(COMPILER) , dest = $(DEST)
	$(COMPILER) -o $(DEST) -m $(MAPFILE) $(TCC_SRCFILES) -DUSE_LOCAL_HEAP
