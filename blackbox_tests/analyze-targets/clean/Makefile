CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Isrc

OBJS := src/main.o src/tokenizer.o src/word_count.o

all: wordstat

wordstat: $(OBJS)
	$(CXX) -o $@ $(OBJS)

src/%.o: src/%.cpp src/tokenizer.h src/word_count.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f wordstat src/*.o

.PHONY: all clean
