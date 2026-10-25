include ../common.mk

# CHECKED_BUFFER turns on code that only exists in the checked build, and
# config.h is force-included. Both must reach the analyzer for its bugs to
# be found.
CORE_FLAGS := -DCHECKED_BUFFER -include ../config.h -I.

OBJS := buffer.o codec_fast.o codec_safe.o

all: libcore.a

libcore.a: $(OBJS)
	ar rcs $@ $^

buffer.o: buffer.cpp buffer.h
	$(CXX) $(CXXFLAGS) $(CORE_FLAGS) -c buffer.cpp -o $@

# The same source file is compiled twice, with different options.
codec_fast.o: codec.cpp buffer.h
	$(CXX) $(CXXFLAGS) $(CORE_FLAGS) -DCODEC_FAST -c codec.cpp -o $@

codec_safe.o: codec.cpp buffer.h
	$(CXX) $(CXXFLAGS) $(CORE_FLAGS) -c codec.cpp -o $@

clean:
	rm -f *.o libcore.a

.PHONY: all clean
