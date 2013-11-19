IDIRS=-I../brotli/dec/ -I../brotli/enc/ -I../

GFLAGS=-no-canonical-prefixes -fno-omit-frame-pointer -fno-tree-vrp -m64

CPP = g++
LFLAGS =
CPPFLAGS = -c $(IDIRS) -std=c++0x $(GFLAGS)

%.o : %.c
	$(CPP) $(CPPFLAGS) $< -o $@
