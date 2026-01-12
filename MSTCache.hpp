#ifndef MST_CACHE_HPP
#define MST_CACHE_HPP

#include "Tree.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

/**
 * @brief MST Cache structure (separate cache for each algorithm)
 * 
 * Stores cached MST computation results for Prim and Kruskal algorithms.
 * Each cache maintains the MST object and all computed values.
 */
struct MSTCache {
  std::atomic<unsigned long long> lastComputedVersion{0};  // Version when MST was last computed
  std::unique_ptr<Tree> cachedMST{nullptr};  // Cached MST object
  std::string cachedMSTString{""};  // Cached MST string representation
  long long cachedTotalWeight{-1};  // Cached total weight
  long long cachedDiameter{-1};  // Cached diameter
  double cachedAverageDistance{-1.0};  // Cached average distance
  std::string cachedShortestPath{""};  // Cached shortest path
};

/**
 * @brief MST Cache Manager
 * 
 * Manages global MST cache for both Prim and Kruskal algorithms.
 * Provides thread-safe cache operations and version tracking.
 */
namespace MSTCacheManager {
  // Global cache variables (shared across all clients in the same server process)
  extern std::atomic<unsigned long long> graphVersion;  // Increments when graph changes
  extern MSTCache primCache;  // Cache for Prim algorithm
  extern MSTCache kruskalCache;  // Cache for Kruskal algorithm
  extern std::mutex cacheLock;  // Mutex for cache operations
  
  /**
   * @brief Invalidate both Prim and Kruskal caches
   * 
   * Clears all cached MST data for both algorithms when graph changes.
   * Thread-safe operation.
   */
  void invalidateAllCaches();
  
  /**
   * @brief Get the cache for a specific algorithm
   * 
   * @param algorithm Algorithm name ("Prim" or "Kruskal")
   * @return Pointer to the appropriate cache, or nullptr if invalid algorithm
   */
  MSTCache* getCache(const std::string& algorithm);
  
  /**
   * @brief Check if cache is valid for current graph version
   * 
   * @param cache The cache to check
   * @param currentVersion Current graph version
   * @return true if cache is valid, false otherwise
   */
  bool isCacheValid(MSTCache* cache, unsigned long long currentVersion);
  
  /**
   * @brief Increment graph version (call when graph changes)
   * 
   * This should be called when the graph is modified (Newgraph, AddEdge, RemoveEdge).
   * Automatically invalidates all caches.
   */
  void incrementGraphVersion();
}

#endif // MST_CACHE_HPP
