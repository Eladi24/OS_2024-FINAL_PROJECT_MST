# 📋 Responsibility Separation Summary

## Clear Boundaries

### 🖥️ Server (handleCommands) Responsibilities

**Handles:**
- ✅ **Input Parsing** - Extract integers from stringstream
- ✅ **Format Validation** - Are values valid? Enough parameters?
- ✅ **I/O Operations** - Multi-packet reading, client communication
- ✅ **Mathematical Constraints** - Max edges validation (format-level)

**Error Types:**
- "Invalid input format" - Can't parse integers
- "Invalid graph parameters" - Mathematical impossibility (m > maxEdges)
- "Error reading edges" - I/O failure

**Does NOT handle:**
- ❌ Graph state checks
- ❌ Graph operations
- ❌ Business logic errors

---

### ⚙️ ActiveObject Responsibilities

**Handles:**
- ✅ **Graph State Validation** - Is graph initialized? (can change!)
- ✅ **Graph Operations** - addEdge, removeEdge, createMST
- ✅ **Business Logic Errors** - Edge exists, invalid vertices, etc.
- ✅ **Concurrency Control** - Lock management

**Error Types:**
- "Graph not initialized" - Graph state error
- "Edge already exists" - Business logic error
- "Graph is being used" - Concurrency error

**Does NOT handle:**
- ❌ Input parsing
- ❌ Format validation
- ❌ Client communication (only sets response string)

---

## Why This Separation?

### 1. **Graph State Can Change**
```cpp
// Time T1: Server checks graph (not initialized)
// Time T2: Another client initializes graph
// Time T3: ActiveObject executes (graph IS initialized now!)
```
**Solution:** ActiveObject checks graph state (at execution time)

### 2. **Format Errors Don't Need Graph Access**
```cpp
// "AddEdge abc def" - Can reject immediately (format error)
// No need to enqueue, no need for graph lock
```
**Solution:** Server validates format before enqueueing

### 3. **Business Logic Needs Graph Access**
```cpp
// "AddEdge 1 2 10" - Format OK, but edge might already exist
// Need graph lock to check
```
**Solution:** ActiveObject handles business logic (has graph access)

---

## Code Structure

```cpp
// ========== SERVER RESPONSIBILITY ==========
if (cmd == "AddEdge") {
  // Parse input
  int u, v, w;
  if (!(ss >> u >> v >> w)) {
    // SERVER ERROR: Format
    sendResponse("Invalid format");
    return;
  }
  
  // Enqueue to ActiveObject
  pipeline[0]->enqueue([...]() {
    // ========== ACTIVEOBJECT RESPONSIBILITY ==========
    lock(graphLock);
    
    // Check graph state
    if (graph not initialized) {
      // ACTIVEOBJECT ERROR: State
      setResponse("Graph not initialized");
      return;
    }
    
    // Perform operation
    if (!addEdge(u, v, w)) {
      // ACTIVEOBJECT ERROR: Business logic
      setResponse("Edge already exists");
      return;
    }
    
    setResponse("Success");
  });
  
  // SERVER: Wait and send
  waitForCompletion();
  sendResponse(future);
}
```

---

## Benefits

✅ **Clear Separation** - Easy to see who handles what
✅ **Consistent** - All graph errors from ActiveObject, all format errors from Server
✅ **Maintainable** - Change graph logic? Only touch ActiveObject
✅ **Testable** - Can test Server parsing separately from ActiveObject logic

---

## Interview Answer

**Q: "How do you separate responsibilities between Server and ActiveObject?"**

**A:** "The Server handles I/O and format validation - parsing input, validating format, communicating with clients. The ActiveObject handles all graph-related logic - state validation, operations, and business errors. This separation ensures format errors are caught early (before enqueueing), while graph state and business logic errors are handled where the graph is accessible (inside ActiveObject). The key insight is that graph state can change between enqueue and execution, so state checks must happen in ActiveObject, not Server."
