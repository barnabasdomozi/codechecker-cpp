include ../common.mk

# Include paths and macros are passed in a response file.
all: cli

cli: cli.o ../core/libcore.a
	$(CXX) -o $@ cli.o ../core/libcore.a

cli.o: cli.cpp flags.rsp
	$(CXX) $(CXXFLAGS) @flags.rsp -c cli.cpp -o $@

clean:
	rm -f cli cli.o

.PHONY: all clean
