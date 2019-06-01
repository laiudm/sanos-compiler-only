#
# Makefile for Compiling the Compiler under Windows using cc.exe (made by hacking Makefile.cross & underlying directories)
#

CCPREFIX=$(CURDIR)
#CCPREFIX=C:\Dave2015\sanos\sanos-src-latest\src. Can't use this path as the lib path doesn't have the built library, just the source. Need a deep path into the cross-built version.
#CCPREFIX=C:\Dave2015\sanos\sanos-src-latest-cross\cross\install\usr

all:
	make -C $(CURDIR)/cc clean
	make -C $(CURDIR)/cc

cmp:
	echo got here
	make -C $(CURDIR)/cmp clean
	make -C $(CURDIR)/cmp
	make -C $(CURDIR)/cmp install

compile:
	echo top-level compile
	make -C $(CURDIR)/cc compile COMPILER=cc        DEST=$(CURDIR)/bin/cc-stage1.exe MAPFILE=cc-stage1MAPFILE
#	dumpbin /ALL /DISASM bin/cc-stage1.exe > bin\cc-stage1.dumpbin
	make -C $(CURDIR)/cc compile COMPILER=cc-stage1 DEST=$(CURDIR)/bin/cc-stage2.exe MAPFILE=cc-stage2MAPFILE
	make -C $(CURDIR)/cc compile COMPILER=cc-stage2 DEST=$(CURDIR)/bin/cc-stage3.exe MAPFILE=cc-stage3MAPFILE
	cmp $(CURDIR)/bin/cc-stage2.exe $(CURDIR)/bin/cc-stage3.exe 0x8c

unittest:
	cc-stage3 -o bin/unittest.exe test/$(UNITTEST).c test/testmain.c
	unittest.exe
	rm bin/unittest.exe
	
test:
	make unittest UNITTEST=arith
	make unittest UNITTEST=array
	make unittest UNITTEST=assign
	make unittest UNITTEST=bitop
	make unittest UNITTEST=cast
	make unittest UNITTEST=comp
	make unittest UNITTEST=constexpr
	make unittest UNITTEST=control
	make unittest UNITTEST=conversion
	make unittest UNITTEST=decl
	make unittest UNITTEST=enum
	make unittest UNITTEST=extern
	make unittest UNITTEST=for
	make unittest UNITTEST=funcargs
	make unittest UNITTEST=function
	make unittest UNITTEST=generic
	make unittest UNITTEST=global
	make unittest UNITTEST=includeguard
	make unittest UNITTEST=int
	make unittest UNITTEST=number
	make unittest UNITTEST=pointer
	make unittest UNITTEST=scope
	make unittest UNITTEST=stmtexpr
	make unittest UNITTEST=typeof
	make unittest UNITTEST=union
	make unittest UNITTEST=usualconv
	make unittest UNITTEST=varargs
	
.PHONY: cmp compile unittest test

