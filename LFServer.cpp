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
#include "LFThreadPool.hpp"

const int port = 4050;
function<void(int)> signalHandlerLambda;
int clientNumber = 0;

void signalHandler(int signum)
{
    cout << "Interrupt signal (" << signum << ") received.\n";
    // Free memory
    signalHandlerLambda(signum);
    exit(signum);
}

int scanGraph(int &n, int &m, stringstream &ss, unique_ptr<Graph> &g)
{

    if (!(ss >> n >> m) || n <= 0 || m < 0)
    {
        cerr << "Invalid graph input" << endl;
        return -1;
    }

    g = make_unique<Graph>(n, m);
    return 0;
}

int scanSrcDest(stringstream &ss, int &src, int &dest)
{
    if (!(ss >> src >> dest) || src < 0 || dest < 0)
    {
        cerr << "Invalid source or destination" << endl;
        return -1;
    }

    return 0;
}


int acceptConnection(int server_sock)
{
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
    if (client_sock == -1)
    {
        perror("accept");
        return -1;
    }
    clientNumber++;
    char s[INET6_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, s, sizeof s);
    cout << "New connection from " << s << " on socket " << client_sock << endl;
    cout << "Currently " << clientNumber << " clients connected" << endl;
    return client_sock;
}

void setupLFThreadPool(LFThreadPool &pool)
{
   // Create event handler for Creating graph
}


int main()
{
    signal(SIGINT, signalHandler);
    Reactor reactor;
    LFThreadPool pool(4, &reactor);
    unique_ptr<Graph> g;
    unique_ptr<Tree> t;
    MSTFactory factory;
    signalHandlerLambda = [&](int signum) {
        cout << "Freeing memory" << endl;
        g.reset();
        t.reset();
    };
     // The server socket
    int serverSock;
    struct sockaddr_in serverAddr;
    // The opt variable is used to set the socket options
    int opt = 1;

    // If the server socket cannot be created, the program will exit
    if ((serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Socket creation error" << endl;
        exit(1);
    }

    // If the cannot set the socket to reuse the address, the program will exit
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        cerr << "Setsockopt error" << endl;
        exit(1);
    }

    // Set the server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    memset(&(serverAddr.sin_zero), '\0', 8);

    // If the server cannot bind to the address, the program will exit
    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind error" << endl;
        exit(1);
    }

    // If the server cannot listen for incoming connections, the program will exit
    if (listen(serverSock, 10) < 0)
    {
        cerr << "Listen error" << endl;
        exit(1);
    }

    cout << "MST server waiting for requests on port " << port << endl;


}