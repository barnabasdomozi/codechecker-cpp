# Produces the output of tools other than CodeChecker in out/, as input for
# 'report-converter'. This is not a build to log with 'CodeChecker log'.
#
# The sanitized programs crash or exit non-zero on purpose; the leading '-'
# keeps make going.

CXX  := clang++
OUT  := out
SAN  := -g -O0

all: asan ubsan lsan clang-tidy gcc

$(OUT):
	mkdir -p $(OUT)

asan: | $(OUT)
	$(CXX) $(SAN) -fsanitize=address src/heap_overflow.cpp -o $(OUT)/heap_overflow
	-$(OUT)/heap_overflow 2> $(OUT)/heap_overflow.asan.out
	$(CXX) $(SAN) -fsanitize=address src/use_after_free.cpp -o $(OUT)/use_after_free
	-$(OUT)/use_after_free 2> $(OUT)/use_after_free.asan.out

ubsan: | $(OUT)
	$(CXX) $(SAN) -fsanitize=undefined src/undefined.cpp -o $(OUT)/undefined
	-$(OUT)/undefined 2> $(OUT)/undefined.ubsan.out

lsan: | $(OUT)
	$(CXX) $(SAN) -fsanitize=leak src/leak.cpp -o $(OUT)/leak
	-$(OUT)/leak 2> $(OUT)/leak.lsan.out

clang-tidy: | $(OUT)
	-clang-tidy src/analyzer_input.cpp -checks='-*,clang-analyzer-*' \
	  -export-fixes=$(OUT)/clang-tidy-fixes.yaml -- -std=c++17 > $(OUT)/clang-tidy.out

gcc: | $(OUT)
	cd $(OUT) && g++ -std=c++17 -fanalyzer -fdiagnostics-format=sarif-file \
	  -c ../src/analyzer_input.cpp -o /dev/null

clean:
	rm -rf $(OUT)

.PHONY: all asan ubsan lsan clang-tidy gcc clean
