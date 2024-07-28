# Compiler g++
CXX = g++
# Compiler flags
CXXFLAGS = -std=c++17 -Wall -g
# Valgrind flags
Valgrind_FLAGS = VALGRIND_FLAGS=--leak-check=full --show-leak-kinds=all --error-exitcode=99 --track-origins=yes --verbose --log-file=valgrind-out.txt
# Source files
SRC = $(wildcard *.cpp)
# Object files
OBJ = $(SRC:.cpp=.o)
# Executable
EXEC = main

# Compile
all: $(EXEC)

$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Dependencies
$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) -c $^

# Run
run: $(EXEC)
	./$(EXEC)

# Rebuild
rebuild: clean all

# Phony
.PHONY: clean all run

# Clean
clean:
	rm -f $(OBJ) $(EXEC) valgrind-out.txt