CXX      := g++
CXXFLAGS := -std=c++17 -Isrc

OBJS := src/main.o src/settings.o src/math.o src/pipeline_a.o src/pipeline_b.o src/pipeline_c.o

all: ctu-demo

ctu-demo: $(OBJS)
	$(CXX) -o $@ $(OBJS)

src/%.o: src/%.cpp src/api.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f ctu-demo src/*.o

.PHONY: all clean
