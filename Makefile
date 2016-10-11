OS := $(shell uname)

BROTLI_ROOT = ./brotli

CPPFLAGS = -I$(BROTLI_ROOT)/dec -I$(BROTLI_ROOT)/enc -I./src

CC ?= gcc
CXX ?= g++

COMMON_FLAGS = -fno-omit-frame-pointer -no-canonical-prefixes -DFONT_COMPRESSION_BIN -D __STDC_FORMAT_MACROS

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  COMMON_FLAGS += -fno-tree-vrp
endif

CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++11

SRCDIR = src

OUROBJ = font.o glyph.o normalize.o table_tags.o transform.o \
         woff2_dec.o woff2_enc.o woff2_common.o woff2_out.o \
         variable_length.o

BROTLI_OBJ_DIR = $(BROTLI_ROOT)/bin/obj
BROTLI_OBJS = $(BROTLI_OBJ_DIR)/common/*.o $(BROTLI_OBJ_DIR)/enc/*.o $(BROTLI_OBJ_DIR)/dec/*.o

OBJS = $(patsubst %, $(SRCDIR)/%, $(OUROBJ))
EXECUTABLES = woff2_compress woff2_decompress

EXE_OBJS = $(patsubst %, $(SRCDIR)/%.o, $(EXECUTABLES))

TEST_FILES = $(wildcard ./test/*.sh)

ifeq (,$(wildcard $(BROTLI_ROOT)/*))
  $(error Brotli dependency not found : you must initialize the Git submodule)
endif

all : $(OBJS) $(EXECUTABLES) test

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LFLAGS) $(OBJS) $(BROTLI_OBJS) $(SRCDIR)/$@.o -o $@

deps :
	$(MAKE) -C $(BROTLI_ROOT)

test_clean :
	rm -rf test/tmp

test : $(TEST_FILES) $(EXECUTABLES) test_clean
	for test in $(TEST_FILES); do \
		$$test; \
	done

clean : test_clean
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	$(MAKE) -C $(BROTLI_ROOT) clean
