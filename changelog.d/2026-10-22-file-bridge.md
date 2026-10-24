- **File bridge for script hosts** (`raw/platform/file_bridge.hpp`, Windows). Five synchronous calls
  (read, write, rename, remove, list) over one root folder. Writes flush to disk, rename replaces
  the target in one step with write-through, and paths that escape the root are refused.
  `tests/test_file_bridge.cpp` covers the round trip, replace, exact bytes, refusal and a 3 MB file.
  Used by the Low Water native host spike; Linux has no implementation yet.
