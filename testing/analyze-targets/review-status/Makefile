CXX      := g++
CXXFLAGS := -std=c++17

# 'make' builds the main sources. 'make ambiguous' builds the file whose
# comments are ambiguous; it makes 'parse' fail, so it is kept separate.
all: comments.o rules_target.o

ambiguous: extra/ambiguous.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o extra/*.o

.PHONY: all ambiguous clean
