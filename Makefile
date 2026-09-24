CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -g -Isrc
SOURCES = src/domain/page_table.cpp src/domain/physical_memory.cpp src/domain/virtual_memory.cpp \
	      src/application/simulator.cpp src/infrastructure/program_file.cpp
PROGRAM ?= data/example.txt

# Se recompila todo cada vez: el proyecto es pequeño y así nunca queda nada desactualizado.
.PHONY: all run test memcheck clean

all:
	$(CXX) $(CXXFLAGS) src/main.cpp $(SOURCES) -o vmsim

run: all
	./vmsim $(PROGRAM) --trace

test:
	$(CXX) $(CXXFLAGS) tests/tests.cpp $(SOURCES) -o tests/run_tests
	./tests/run_tests

memcheck: all
	valgrind --leak-check=full --error-exitcode=1 ./vmsim $(PROGRAM)

clean:
	rm -rf vmsim vmsim.dSYM tests/run_tests tests/run_tests.dSYM
