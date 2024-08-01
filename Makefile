# Compiler g++
CXX = g++
# Compiler flags
CXXFLAGS = -std=c++17 -Wall -g
# Valgrind flags
Valgrind_FLAGS = VALGRIND_FLAGS=--leak-check=full --show-leak-kinds=all --error-exitcode=99 --track-origins=yes --verbose --log-file=valgrind-out.txt
# Tree Library source files
LIB_SRC = Graph.cpp Tree.cpp MSTStrategy.cpp MSTFactory.cpp
# Tree Library object files
LIB_OBJ = $(LIB_SRC:.cpp=.o)
# Tree Library target
LIB_TARGET = libTree.so
# Pipeline Server source files
PIP_SRC = PipelineServer.cpp ActiveObject.cpp
# Pipeline Server object files
PIP_OBJ = $(PIP_SRC:.cpp=.o)

# Compile
all: PipelineServer Demo

PipelineServer: $(LIB_TARGET) $(PIP_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(PIP_OBJ) ./$(LIB_TARGET)
	
Demo: $(LIB_TARGET) Demo.o
	$(CXX) $(CXXFLAGS) -o $@ Demo.o ./$(LIB_TARGET)

# Library
$(LIB_TARGET): $(LIB_OBJ)
	$(CXX) -shared -o $@ $(LIB_OBJ)

# Object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -fPIC -c $<

# Rebuild
rebuild: clean all

# Phony
.PHONY: clean all run

# Clean
clean:
	rm -f *.o *.so PipelineServer Demo valgrind-out.txt