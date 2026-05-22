# Makefile include for something else

CFILES += prims/somethingelse.c
OFILES += prims/somethingelse.o

prims/somethingelse.o : prims/somethingelse.c es.h config.h stdenv.h prim.h
