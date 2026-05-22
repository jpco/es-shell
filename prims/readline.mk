# Makefile include for readline primitives

# TODO: make this work with @READLINE_CFLAGS@ and @READLINE_LIBS@
LIBS	+= -lreadline
CFILES	+= prims/readline.c
OFILES	+= prims/readline.o

prims/readline.o : prims/readline.c es.h config.h stdenv.h prim.h
