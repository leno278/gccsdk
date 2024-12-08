# Copyright (c) 2019-2024 GCCSDK developers

bin_PROGRAMS=ARMEABISupport
ARMEABISupport_SOURCES= \
	link_list.c \
	link_list.h \
	memory.c \
	memory.h \
	swihandler.c \
	init-fini.c \
	main.c \
	swi.c \
	swi.h \
	swi-asm.s \
	command.c \
	abort.c \
	abort.h \
	abort-asm.s \
	stack.c \
	stack.h \
	stack-asm.s \
	mmap.c \
	mmap.h \
	mmap-asm.s \
	shm.c \
	shm.h \
	fdop.c \
	fdop.h \
	array.c \
	array.h \
	filetable.c \
	filetable.h \
	shmtable.c \
	shmtable.h \
	fdtable.c \
	fdtable.h \
	fileop.c \
	fileop.h \
	lock.c \
	lock.h \
	armeabisupport.cmhg

include_HEADERS=armeabi-support.h
BUILT_SOURCES=armeabisupport.h
ARMEABISupport_CFLAGS=-O3 -mmodule -Wall -std=gnu99
ARMEABISupport_CCASFLAGS=-O3 -mmodule -Wall -std=gnu99 -xassembler-with-cpp
ARMEABISupport_LDFLAGS=-mmodule

# CMunge invocation:
SUFFIXES=.cmhg
CMUNGEFLAGS = -tgcc -32bit -p -DPACKAGE_VERSION=$(PACKAGE_VERSION)
.cmhg.h:
	cmunge $(CMUNGEFLAGS) -d $@ $<
.cmhg.o:
	cmunge $(CMUNGEFLAGS) -o $@ $<

# Add ,ffa suffix to RISC OS Module filename during installation:
install-exec-hook:
	test -f "$(DESTDIR)$(bindir)/ARMEABISupport$(EXEEXT)" && mv $(DESTDIR)$(bindir)/ARMEABISupport$(EXEEXT) $(DESTDIR)$(bindir)/ARMEABISupport$(EXEEXT)\,ffa
	mkdir -p $(GCCSDK_INSTALL_ENV)/include
	cp -T ${srcdir}/armeabi-support.h $(GCCSDK_INSTALL_ENV)/include/armeabi-support.h
