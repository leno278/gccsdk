bin_PROGRAMS=decaof

GIT_REV := $(shell git describe 2>/dev/null | cat)
SVN_ROOT := $(shell svn info ${srcdir} | grep "^Repository Root: " | sed -e "s/^Repository Root: //")
SVN_BRANCH := $(shell svn info ${srcdir} | grep "^URL: " | sed -e "s/^URL: //")
SVN_REV := r$(shell svnversion -n ${srcdir}) at $(subst $(SVN_ROOT),,$(SVN_BRANCH))

decaof_CFLAGS = -std=c99 \
	-W -Wall -pedantic -Wbad-function-cast -Wcast-align \
	-Wchar-subscripts -Winline \
	-Wmissing-prototypes -Wnested-externs -Wpointer-arith \
	-Wredundant-decls -Wshadow -Wstrict-prototypes -Wwrite-strings \
	-DGCCSDK_REVISION="\"$(if $(GIT_REV),$(GIT_REV),$(SVN_REV))\""
decaof_SOURCES=decode.c error.c io.c main.c misc.c
