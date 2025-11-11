# 🕸️ Minimal Spanning Tree (MST) Project

## 📘 Introduction

This project is designed to tackle the **Minimal Spanning Tree (MST)** problem on a **weighted directed graph**, showcasing a wide range of concepts learned throughout the course.
It combines **algorithmic design**, **system architecture**, and **performance analysis** into one cohesive system.

The implementation demonstrates:

* **Algorithmic understanding** through MST algorithms (Prim & Kruskal)
* **Software engineering practices** via design patterns
* **Concurrent server design** using multi-threading models
* **Memory and performance validation** with Valgrind tools

---

## ⚙️ Key Components

### 🧩 Graph Data Structure

A flexible data structure was implemented to represent **weighted directed graphs**, supporting efficient MST computation and traversal.

### 🔢 MST Algorithms

The project includes two main algorithms:

* **Prim’s Algorithm** — Greedy approach for growing the MST from a starting vertex.
* **Kruskal’s Algorithm** — Greedy approach that builds the MST by sorting and adding edges.

Both algorithms were tested and compared for performance and correctness.

### 🌐 Server Implementation

A **server component** was developed to:

* Receive graph data and MST computation requests.
* Process graphs and return MST metrics dynamically.
  This structure mimics real-world **client-server communication** systems.

### 🧵 Thread Management

Concurrency was introduced using two threading models:

* **Active Object Pattern** — Decouples method invocation from execution for better concurrency.
* **Thread Pool (Leader-Follower)** — Reuses worker threads to handle multiple simultaneous MST requests.

These models improve efficiency and scalability.

### 🧠 Valgrind & Helgrind Analysis

Using **Valgrind** and **Helgrind**, we verified:

* Memory management and leak detection
* Thread safety and synchronization correctness
* General performance profiling

### ✅ Code Coverage

Comprehensive testing and coverage ensured correctness and robustness of all modules.

---

## 🧪 How to Run

1. **Clone the repository**

   ```bash
   git clone https://github.com/your-username/mst-project.git
   cd mst-project
   ```

2. **Compile the project**

   ```bash
   make all
   ```

3. **Run the server**

   ```bash
   ./server
   ```

4. **Send MST requests**

   * Use a client script or tool to send graph data and request MST computation.
   * The server will process and return MST results and metrics.

---

## 📊 Tools & Technologies

* **C++ (OOP, STL, Threads)**
* **Socket Programming**
* **Valgrind / Helgrind**
* **Makefile**
* **Thread Pool & Active Object Design Patterns**

---

## 📚 Learning Outcomes

Through this project, we:

* Strengthened understanding of **graph algorithms and complexity**
* Gained hands-on experience in **multi-threaded systems**
* Practiced **memory management and debugging**
* Learned to structure, test, and document **large-scale software projects**

---

## 📄 Project Documentation

For a detailed explanation of the project design, architecture, and results, refer to the full documentation here:
📄 **[Project Overview Document](https://docs.google.com/document/d/1bfcD3win1w8mH7emu1Vbk3biR1qkCw0EYVcYm5qs5V0/edit?usp=sharing)**

---

## 👥 Authors

**Names:** Elad Imany & Vivian Umansky

---

