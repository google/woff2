OS := $(shell uname)

CPPFLAGS = -I./brotli/dec/ -I./brotli/enc/ -I./src

CC ?= gcc
CXX ?= g++

COMMON_FLAGS = -fno-omit-frame-pointer -no-canonical-prefixes -DFONT_COMPRESSION_BIN

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  COMMON_FLAGS += -fno-tree-vrp
endif

CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++11

SRCDIR = src

OUROBJ = font.o glyph.o normalize.o table_tags.o transform.o \
         woff2_dec.o woff2_enc.o woff2_common.o variable_length.o

BROTLI = brotli
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

OBJS = $(patsubst %, $(SRCDIR)/%, $(OUROBJ))
EXECUTABLES=woff2_compress woff2_decompress

EXE_OBJS=$(patsubst %, $(SRCDIR)/%.o, $(EXECUTABLES))

all : $(OBJS) $(EXECUTABLES)

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LFLAGS) $(OBJS) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o -o $@

deps :
	$(MAKE) -C $(BROTLI)/dec
	$(MAKE) -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	$(MAKE) -C $(BROTLI)/dec clean
	$(MAKE) -C $(BROTLI)/enc clean
