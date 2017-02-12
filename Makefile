OS := $(shell uname)

CPPFLAGS = -I./brotli/dec/ -I./brotli/enc/ -I./src

AR ?= ar
CC ?= gcc
CXX ?= g++

# It's helpful to be able to turn this off for fuzzing
CANONICAL_PREFIXES ?= -no-canonical-prefixes
COMMON_FLAGS = -fno-omit-frame-pointer $(CANONICAL_PREFIXES) -DFONT_COMPRESSION_BIN -D __STDC_FORMAT_MACROS

ARFLAGS = cr

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  COMMON_FLAGS += -fno-tree-vrp
  ARFLAGS += f
endif


CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++11

SRCDIR = src

OUROBJ = font.o glyph.o normalize.o table_tags.o transform.o \
         woff2_dec.o woff2_enc.o woff2_common.o woff2_out.o \
         variable_length.o

BROTLI = brotli
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

OBJS = $(patsubst %, $(SRCDIR)/%, $(OUROBJ))
EXECUTABLES=woff2_compress woff2_decompress
EXE_OBJS=$(patsubst %, $(SRCDIR)/%.o, $(EXECUTABLES))
ARCHIVES=convert_woff2ttf_fuzzer convert_woff2ttf_fuzzer_new_entry
ARCHIVE_OBJS=$(patsubst %, $(SRCDIR)/%.o, $(ARCHIVES))

ifeq (,$(wildcard $(BROTLI)/*))
  $(error Brotli dependency not found : you must initialize the Git submodule)
endif

all : $(OBJS) $(EXECUTABLES) $(ARCHIVES)

$(ARCHIVES) : $(ARCHIVE_OBJS) $(OBJS) deps
	$(AR) $(ARFLAGS) $(SRCDIR)/$@.a $(OBJS) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LFLAGS) $(OBJS) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o -o $@

deps :
	$(MAKE) -C $(BROTLI)/dec
	$(MAKE) -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES) $(ARCHIVE_OBJS)
	$(MAKE) -C $(BROTLI)/dec clean
	$(MAKE) -C $(BROTLI)/enc clean
