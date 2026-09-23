# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Offline audio generator. `make` builds the tools; `./build.sh` renders and
# encodes every asset. -ffp-contract=off keeps floating point free of fused
# multiply-adds, so a given toolchain renders the same samples every time.
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Werror -ffp-contract=off
LDLIBS  += -lm

GEN_OBJ = main.o dsp.o voices.o sfx.o music.o
ANA_OBJ = analyze.o dsp.o

all: audiogen analyze

audiogen: $(GEN_OBJ)
	$(CC) $(CFLAGS) -o $@ $(GEN_OBJ) $(LDLIBS)

analyze: $(ANA_OBJ)
	$(CC) $(CFLAGS) -o $@ $(ANA_OBJ) $(LDLIBS)

%.o: %.c dsp.h voices.h sfx.h music.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o audiogen analyze

.PHONY: all clean
