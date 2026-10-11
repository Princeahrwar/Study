# tests/workspace/

Empty on purpose until you've filled in a file under `../../src/workspace/`.
When you have:

1. Add a `<problem>_test.cpp` here — gtest, `#include "workspace/<problem>.cpp"`
   directly (unconventional, but fine for a single-TU workspace stub with
   no separate header) or split your stub into a `.hpp` + `.cpp` first if
   you'd rather do it the normal way.
2. Uncomment and adapt the example block at the bottom of
   `../CMakeLists.txt`, pointing `add_executable` at your new test file and
   the matching `src/workspace/<problem>.cpp`.
3. From the `lld/` directory: `cmake --build build && ctest --test-dir build --output-on-failure`.
4. Pull a few cases straight from that problem's tutorial "Step 6 — Edge
   cases handled" section — those are exactly the cases worth asserting on.

See `../../src/workspace/README.md` for the full workflow this supports
(implement cold, test your own work, *then* diff against the tutorial).
