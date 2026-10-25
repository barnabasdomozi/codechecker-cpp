CXX      := g++
CXXFLAGS := -std=c++17

# The sources that compile come first. Plain 'make' stops at
# broken_syntax.cpp (exit 2); 'make -k' also tries missing_header.cpp.
OBJS := good.o gcc_only_flag.o ignored_flag.o broken_syntax.o missing_header.o

all: $(OBJS)

# Clang rejects this GCC option and CodeChecker does not remove it, so the
# analysis of this file fails although the build succeeds.
gcc_only_flag.o: gcc_only_flag.cpp
	$(CXX) $(CXXFLAGS) -fconcepts-diagnostics-depth=2 -c $< -o $@

# Clang rejects this GCC option too, but it is on CodeChecker's GCC ignore
# list, so the analysis succeeds.
ignored_flag.o: ignored_flag.cpp
	$(CXX) $(CXXFLAGS) -fsched-pressure -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o

.PHONY: all clean
