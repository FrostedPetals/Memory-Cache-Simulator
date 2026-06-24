Key features


- Wrote the MakePinTool.cpp for memory trace
- Observed that tracing captures all memory accesses (including system libraries), resulting in significant noise
- Implemented a filtering system that captures only main executable
- Integrated a cache simulator into the Pintool to process memory accesses during execution
- Implemented a set-associative cache model with configurable parameters (cache size, block size, associativity)
- Computed cache hits and misses in real-time for each memory access
- Added sample files for dashboard visualization on charts of how LRU,LFU and FIFO differ in performance
