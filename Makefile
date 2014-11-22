#Converter makefile

include shared.mk

SRCDIR = src

OUROBJ = font.o glyph.o normalize.o table_tags.o transform.o \
         woff2_dec.o woff2_enc.o

BROTLI = brotli
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

OBJS = $(patsubst %, $(SRCDIR)/%, $(OUROBJ))
EXECUTABLES=woff2_compress woff2_decompress

EXE_OBJS=$(patsubst %, $(SRCDIR)/%.o, $(EXECUTABLES))

all : $(OBJS) $(EXECUTABLES)

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CPP) $(LFLAGS) $(OBJS) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o -o $@

deps :
	make -C $(BROTLI)/dec
	make -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	make -C $(BROTLI)/dec clean
	make -C $(BROTLI)/enc clean
