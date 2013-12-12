OS := $(shell uname)
IDIRS=-I../brotli/dec/ -I../brotli/enc/ -I../

GFLAGS=-no-canonical-prefixes -fno-omit-frame-pointer -m64

CPP = g++
LFLAGS =
CPPFLAGS = -c $(IDIRS) -std=c++0x $(GFLAGS)

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  CPPFLAGS += -fno-tree-vrp
endif

%.o : %.c
	$(CPP) $(CPPFLAGS) $< -o $@
