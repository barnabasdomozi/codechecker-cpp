CXX      := g++
CXXFLAGS := -std=c++17

OBJS := memory.o paths.o macros.o moves.o style.o

all: $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o

.PHONY: all clean
