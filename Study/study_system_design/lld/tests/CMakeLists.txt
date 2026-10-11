# Reference solutions: fully implemented, faithful to the tutorials, always
# built and tested. These are what "diff against the reference" (this
# track's core study habit) actually diffs against.
add_executable(reference_tests
  reference/tic_tac_toe_test.cpp
  reference/lru_cache_test.cpp
)
target_link_libraries(reference_tests PRIVATE lld_reference GTest::gtest_main)
target_include_directories(reference_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
gtest_discover_tests(reference_tests)

# Workspace: your own from-scratch attempts, per the "implement without
# looking" exercise in every tutorial. src/workspace/*.cpp already compiles
# (every method throws "not implemented" until you fill it in) -- this
# target just proves that. Once you've replaced a throw with real logic,
# add your own test file here (see workspace/README.md) and uncomment the
# example block below, adapted to your problem.

# Example, once you've filled in src/workspace/parking_lot.cpp:
# add_executable(parking_lot_workspace_tests
#   workspace/parking_lot_test.cpp
#   ${CMAKE_SOURCE_DIR}/src/workspace/parking_lot.cpp
# )
# target_link_libraries(parking_lot_workspace_tests PRIVATE GTest::gtest_main)
# target_include_directories(parking_lot_workspace_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
# gtest_discover_tests(parking_lot_workspace_tests)
