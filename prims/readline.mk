# Makefile include for readline primitives

CFLAGS	+= 
LIBS	+= -lreadline
CFILES	+= prims/readline.c
OFILES	+= prims/readline.o

prims/readline.o : prims/readline.c es.h config.h stdenv.h prim.h
