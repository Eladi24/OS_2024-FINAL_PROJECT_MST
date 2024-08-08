CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -fPIC

all: PipelineServer LFServer

PipelineServer: PipelineServer.o ActiveObject.o libTree.so
	$(CXX) $(CXXFLAGS) -o PipelineServer PipelineServer.o ActiveObject.o ./libTree.so

LFServer: LFServer.o LFThreadPool.o ActiveObject.o libTree.so
	$(CXX) $(CXXFLAGS) -o LFServer LFServer.o LFThreadPool.o ActiveObject.o ./libTree.so

PipelineServer.o: PipelineServer.cpp
	$(CXX) $(CXXFLAGS) -c PipelineServer.cpp

LFServer.o: LFServer.cpp
	$(CXX) $(CXXFLAGS) -c LFServer.cpp

LFThreadPool.o: LFThreadPool.cpp
	$(CXX) $(CXXFLAGS) -c LFThreadPool.cpp

ActiveObject.o: ActiveObject.cpp
	$(CXX) $(CXXFLAGS) -c ActiveObject.cpp

libTree.so: Graph.o Tree.o MSTStrategy.o MSTFactory.o
	$(CXX) $(CXXFLAGS) -shared -o libTree.so Graph.o Tree.o MSTStrategy.o MSTFactory.o

Graph.o: Graph.cpp
	$(CXX) $(CXXFLAGS) -c Graph.cpp

Tree.o: Tree.cpp
	$(CXX) $(CXXFLAGS) -c Tree.cpp

MSTStrategy.o: MSTStrategy.cpp
	$(CXX) $(CXXFLAGS) -c MSTStrategy.cpp

MSTFactory.o: MSTFactory.cpp
	$(CXX) $(CXXFLAGS) -c MSTFactory.cpp

clean:
	rm -f *.o PipelineServer LFServer libTree.so
