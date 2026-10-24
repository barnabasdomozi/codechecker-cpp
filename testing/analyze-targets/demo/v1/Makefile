all: app style.o
app: main.o lib/util.o
	gcc -o app main.o lib/util.o
main.o: main.c
	gcc -c main.c -o main.o
lib/util.o: lib/util.c
	gcc -c lib/util.c -o lib/util.o
style.o: style.cpp
	g++ -std=c++17 -c style.cpp -o style.o
clean:
	rm -f app *.o lib/*.o
