CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

HDRS = src/event.hpp src/heap_queue.hpp src/sorted_array_queue.hpp \
       src/sorted_list_queue.hpp src/hold_bench.hpp src/park_sim.hpp

all: bench park run_tests run_park_tests

# Etapa 5: benchmark do hold model
bench: src/main.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

# Etapa 6: simulador de parque (benchmark e demo)
park: src/park_main.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ src/park_main.cpp

run_tests: tests/test_queues.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_queues.cpp

run_park_tests: tests/test_park.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_park.cpp

# Roda todos os testes de correcao (faca isso antes de qualquer benchmark).
test: run_tests run_park_tests
	./run_tests
	./run_park_tests

clean:
	rm -f bench park run_tests run_park_tests

.PHONY: all test clean
