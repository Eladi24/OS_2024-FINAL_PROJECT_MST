# Compiler g++
CXX = g++
# Compiler flags
CXXFLAGS = -std=c++17 -Wall -g
# Valgrind flags
Valgrind_FLAGS = valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=99 --track-origins=yes --verbose --log-file=
# Helgrind flags
Helgrind_FLAGS = valgrind --tool=helgrind --error-exitcode=99 --verbose --log-file=
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

LF_SRC = LFServer.cpp LFThreadPool.cpp Reactor.cpp ThreadContext.cpp
LF_OBJ = $(LF_SRC:.cpp=.o)

# Compile
all: PipelineServer LFServer
	
PipelineServer: $(LIB_TARGET) $(PIP_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(PIP_OBJ) ./$(LIB_TARGET) -lpthread

LFServer: $(LIB_TARGET) $(LF_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(LF_OBJ) ./$(LIB_TARGET) -lpthread

# Library
$(LIB_TARGET): $(LIB_OBJ)
	$(CXX) -shared -o $@ $(LIB_OBJ)

# Object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -fPIC -c $<

# Valgrind Pipeline Server
pipeline_valgrind: PipelineServer
	clear
	$(Valgrind_FLAGS)pipeline-valgrind-out.txt ./PipelineServer

# Helgrind Pipeline Server
pipeline_helgrind: PipelineServer
	clear
	$(Helgrind_FLAGS)pipeline-helgrind-out.txt ./PipelineServer

# Valgrind LF Server
lf_valgrind: LFServer
	clear
	$(Valgrind_FLAGS)lf-valgrind-out.txt ./LFServer

# Helgrind LF Server
lf_helgrind: LFServer
	clear
	$(Helgrind_FLAGS)lf-helgrind-out.txt ./LFServer

# Rebuild
rebuild: clean all

clear:
	rm -f *.txt
	
# Phony
.PHONY: clean all rebuild pipeline_valgrind pipeline_helgrind lf_valgrind lf_helgrind

# Clean
clean:
	rm -f *.o *.so *.txt PipelineServer LFServer