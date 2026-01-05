# 🎯 Responsibility Separation Design

## Current Problems

**Mixed Responsibilities:**
- Input parsing errors: Server handles ✓
- Graph state errors: ActiveObject handles ✗ (inconsistent)
- Business logic errors: ActiveObject handles ✓
- Format validation: Server handles ✓

**Result:** Confusing - errors come from different places

---

## Clean Separation

### Server (handleCommands) Responsibilities

**ONLY handles:**
1. **Input Parsing** - Extract data from stringstream
2. **Format Validation** - Are values valid integers? Enough parameters?
3. **Client Communication** - Send responses to client
4. **I/O Operations** - Multi-packet reading

**DOES NOT handle:**
- Graph state checks
- Graph operations
- Business logic errors

### ActiveObject Responsibilities

**ONLY handles:**
1. **Graph State Validation** - Is graph initialized? (can change!)
2. **Graph Operations** - addEdge, removeEdge, createMST
3. **Business Logic Errors** - Edge exists, invalid vertices, etc.
4. **All Graph-Related Errors** - Everything about graph state/logic

**DOES NOT handle:**
- Input parsing
- Format validation
- Client communication (only sets response string)

---

## Proposed Structure

```cpp
// SERVER: Parse input, validate format
if (cmd == "AddEdge") {
  int u, v, w;
  if (!(ss >> u >> v >> w)) {
    // SERVER ERROR: Format issue
    sendResponse("Invalid format");
    continue;
  }
  
  // SERVER: Enqueue to ActiveObject
  pipeline[0]->enqueue([...]() {
    // ACTIVEOBJECT: All graph logic
    if (graph not initialized) {
      // ACTIVEOBJECT ERROR: Graph state
      setResponse("Graph not initialized");
      return;
    }
    if (addEdge fails) {
      // ACTIVEOBJECT ERROR: Business logic
      setResponse("Edge already exists");
      return;
    }
    setResponse("Success");
  });
  
  // SERVER: Wait and send response
  waitForCompletion();
  sendResponse(future);
}
```

---

## Key Principle

**Server = I/O Layer**
- Parses strings
- Validates format
- Communicates with client

**ActiveObject = Business Logic Layer**
- Validates graph state
- Performs operations
- Handles business errors

**Clear boundary:** Server never checks graph state, ActiveObject never parses input
