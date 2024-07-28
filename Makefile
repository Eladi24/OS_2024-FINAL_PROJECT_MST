# Compiler g++
CXX = g++
# Compiler flags
CXXFLAGS = -std=c++17 -Wall -g
# Valgrind flags
Valgrind_FLAGS = VALGRIND_FLAGS=--leak-check=full --show-leak-kinds=all --error-exitcode=99 --track-origins=yes --verbose --log-file=valgrind-out.txt
# Source files
SRC = $(wildcard *.cpp)