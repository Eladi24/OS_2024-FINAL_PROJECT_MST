#include "MSTCache.hpp"
#include <mutex>

namespace MSTCacheManager {

  std::atomic<unsigned long long> graphVersion(0);
  MSTCache primCache;
  MSTCache kruskalCache;
  std::mutex cacheLock;
  
  void invalidateAllCaches() {
    std::unique_lock<std::mutex> guard(cacheLock);
    
  
    primCache.cachedMST.reset();
    primCache.cachedMSTString.clear();
    primCache.cachedTotalWeight = -1;
    primCache.cachedDiameter = -1;
    primCache.cachedAverageDistance = -1.0;
    primCache.cachedShortestPath.clear();
    primCache.lastComputedVersion.store(0, std::memory_order_release);
    
    // Invalidate Kruskal cache
    kruskalCache.cachedMST.reset();
    kruskalCache.cachedMSTString.clear();
    kruskalCache.cachedTotalWeight = -1;
    kruskalCache.cachedDiameter = -1;
    kruskalCache.cachedAverageDistance = -1.0;
    kruskalCache.cachedShortestPath.clear();
    kruskalCache.lastComputedVersion.store(0, std::memory_order_release);
  }
  
  MSTCache* getCache(const std::string& algorithm) {
    if (algorithm == "Prim") {
      return &primCache;
    } else if (algorithm == "Kruskal") {
      return &kruskalCache;
    }
    return nullptr;
  }
  
  bool isCacheValid(MSTCache* cache, unsigned long long currentVersion) {
    if (cache == nullptr) {
      return false;
    }
    
    unsigned long long cachedVersion = cache->lastComputedVersion.load(std::memory_order_acquire);
    return (currentVersion == cachedVersion && 
            cache->cachedMST != nullptr && 
            cache->cachedTotalWeight >= 0);
  }
  
  void incrementGraphVersion() {
    graphVersion.fetch_add(1, std::memory_order_release);
    invalidateAllCaches();
  }
}
