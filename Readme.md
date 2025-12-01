# 🕸️ Minimal Spanning Tree (MST) Project

## Introduction

This project is designed to tackle the **Minimal Spanning Tree (MST)** problem on a weighted directed graph, showcasing a wide range of concepts learned throughout the course. It combines algorithmic design, system architecture, and performance analysis into one cohesive system.

The implementation demonstrates:
* **Algorithmic Understanding:** Implementation of MST algorithms (Prim & Kruskal).
* **Software Engineering:** Usage of design patterns.
* **Concurrency:** Server design using multi-threading models.
* **Validation:** Memory and performance validation with Valgrind tools.

---

## ⚙️ Key Components

### Graph Data Structure
A flexible data structure was implemented to represent weighted directed graphs, supporting efficient MST computation and traversal.

### MST Algorithms
The project includes two main algorithms:
* **Prim’s Algorithm:** Greedy approach for growing the MST from a starting vertex.
* **Kruskal’s Algorithm:** Greedy approach that builds the MST by sorting and adding edges.

### Server Implementations
Two distinct server architectures were developed to handle requests on port `4050`:
1.  **Pipeline Server (`PipelineServer`):** Uses the **Active Object Pattern** to decouple method invocation from execution.
2.  **Leader-Follower Server (`LFServer`):** Uses a Thread Pool with the **Leader-Follower pattern** and a Reactor to handle multiple clients.

### Thread Management
Concurrency was introduced using two specific threading models:
* **Active Object Pattern:** Requests are processed through a pipeline of worker threads.
* **Leader-Follower Pool:** Threads take turns listening for events and processing requests.

### Valgrind & Helgrind Analysis
Using Valgrind and Helgrind, we verified:
* Memory management and leak detection (`memcheck`).
* Thread safety and synchronization correctness (`helgrind`).

### Code Coverage
Comprehensive testing and coverage ensured correctness and robustness of all modules, generated via `gcov` and `lcov`.

---

## How to Run

### 1. Clone and Compile
```bash
git clone https://github.com/Eladi24/OS_2024-FINAL_PROJECT_MST/
cd OS_2024-FINAL_PROJECT_MST
make all
```

### 2. Run a Server
Choose one of the two server implementations to run (both listen on port `4050`):

**Option A: Pipeline Server**
```bash
./PipelineServer
```

**Option B: Leader-Follower Server**
```bash
./LFServer
```

### 3. Send MST Requests (Client Protocol)
Connect to the server using `nc localhost 4050` or a client script.

**Supported Commands:**

| Command | Arguments | Description |
| :--- | :--- | :--- |
| **Newgraph** | `n m` | Initialize graph with `n` vertices and `m` edges. **Must be followed by `m` lines of `u v w`.** |
| **AddEdge** | `u v w` | Add an edge from `u` to `v` with weight `w`. |
| **RemoveEdge** | `u v` | Remove the edge from `u` to `v`. |
| **Prim** | - | Compute MST using Prim's algorithm. |
| **Kruskal** | - | Compute MST using Kruskal's algorithm. |
| **Exit** | - | Close connection. |

**Example Interaction:**
```text
Newgraph 4 5
0 1 10
0 2 6
0 3 5
1 3 15
2 3 4
Prim
```

---

## 📊 Analysis & Debugging

You can run the servers under analysis tools using the provided Makefile targets.

**Memory Check (Valgrind):**
```bash
make pipeline_valgrind
make lf_valgrind
```

**Thread Race Detection (Helgrind):**
```bash
make pipeline_helgrind
make lf_helgrind
```

**Generate Coverage Reports:**
```bash
make lcov
```
*(Generates HTML reports in the `out/` directory)*

---

## Learning Outcomes

Through this project, we:
* Strengthened understanding of graph algorithms and complexity.
* Gained hands-on experience in multi-threaded systems.
* Practiced memory management and debugging.
* Learned to structure, test, and document large-scale software projects.

---

## 📄 Project Documentation

For a detailed explanation of the project design, architecture, and results, refer to the full documentation here:
[📄 Project Overview Document](./path_to_your_doc.pdf)

---

## 👥 Authors

* **Elad Imany**
* **Vivian Umansky**

## 🪪 License

This project is released under the **MIT License** – free to use and modify for educational and learning purposes.

