#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <cstring>

#include "Graph.hpp"
#include "Tree.hpp"
#include "MSTFactory.hpp"
#include "MSTFactory.hpp"
#include "ActiveObject.hpp"

const int port = 4050;

