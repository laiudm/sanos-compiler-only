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

test:
	cc -o test/arith.exe test/arith.c test/testmain.c
	test/arith.exe
	cc -o test/array.exe test/array.c test/testmain.c
	test/array.exe
	cc -o test/assign.exe test/assign.c test/testmain.c
	test/assign.exe
	cc -o test/bitop.exe test/bitop.c test/testmain.c
	test/bitop.exe
	cc -o test/builtin.exe test/builtin.c test/testmain.c
	test/builtin.exe
	cc -o test/cast.exe test/cast.c test/testmain.c
	test/cast.exe
	cc -o test/comp.exe test/comp.c test/testmain.c
	test/comp.exe
	cc -o test/constexpr.exe test/constexpr.c test/testmain.c
	test/constexpr.exe
	cc -o test/control.exe test/control.c test/testmain.c
	test/control.exe
	cc -o test/conversion.exe test/conversion.c test/testmain.c
	test/conversion.exe
	cc -o test/decl.exe test/decl.c test/testmain.c
	test/decl.exe
	cc -o test/enum.exe test/enum.c test/testmain.c
	test/enum.exe
	cc -o test/extern.exe test/extern.c test/testmain.c
	test/extern.exe
	cc -o test/funcargs.exe test/funcargs.c test/testmain.c
	test/funcargs.exe
	cc -o test/function.exe test/function.c test/testmain.c
	test/function.exe
	cc -o test/generic.exe test/generic.c test/testmain.c
	test/generic.exe
	cc -o test/global.exe test/global.c test/testmain.c
	test/global.exe
	cc -o test/includeguard.exe test/includeguard.c test/testmain.c
	test/includeguard.exe
	cc -o test/int.exe test/int.c test/testmain.c
	test/int.exe
	cc -o test/number.exe test/number.c test/testmain.c
	test/number.exe
	cc -o test/pointer.exe test/pointer.c test/testmain.c
	test/pointer.exe
	cc -o test/scope.exe test/scope.c test/testmain.c
	test/scope.exe
	cc -o test/stmtexpr.exe test/stmtexpr.c test/testmain.c
	test/stmtexpr.exe
	cc -o test/typeof.exe test/typeof.c test/testmain.c
	test/typeof.exe
	cc -o test/union.exe test/union.c test/testmain.c
	test/union.exe
	cc -o test/usualconv.exe test/usualconv.c test/testmain.c
	test/usualconv.exe
	cc -o test/varargs.exe test/varargs.c test/testmain.c
	test/varargs.exe
	
.PHONY: cmp compile test

