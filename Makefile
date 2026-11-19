CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

HDRS = $(wildcard src/*.hpp)

all: bench park run_tests run_park_tests

# benchmark do hold model
bench: src/main.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

# benchmark do simulador de parque
park: src/park_main.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

run_tests: tests/test_queues.cpp tests/check.hpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

run_park_tests: tests/test_park.cpp tests/check.hpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Roda todos os testes de correção (faça isso antes de qualquer benchmark).
test: run_tests run_park_tests
	./run_tests
	./run_park_tests

clean:
	rm -f bench park run_tests run_park_tests

.PHONY: all test clean
