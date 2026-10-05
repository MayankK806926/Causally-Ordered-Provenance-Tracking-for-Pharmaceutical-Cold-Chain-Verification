cmake_minimum_required(VERSION 3.20)
project(coldchain_vclock CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Core library -----------------------------------------------------------
add_library(coldchain_core
    src/vector_clock.cpp
    src/events.cpp
    src/hashing.cpp
    src/ledger_block.cpp
    src/physical_clock.cpp
    src/causal_merge_engine.cpp
)
target_include_directories(coldchain_core PUBLIC include)
target_include_directories(coldchain_core PRIVATE third_party)

# --- Simulation executable (Day 5 Week 2 Checkpoint) --------------------------
add_executable(sim src/main_sim.cpp)
target_link_libraries(sim coldchain_core)
target_include_directories(sim PRIVATE third_party)

# --- Tests -------------------------------------------------------------------
enable_testing()

add_executable(test_ledger_integrity tests/test_ledger_integrity.cpp)
target_link_libraries(test_ledger_integrity coldchain_core)
target_include_directories(test_ledger_integrity PRIVATE third_party)
add_test(NAME LedgerIntegrity COMMAND test_ledger_integrity)

add_executable(test_vector_clock tests/test_vector_clock.cpp)
target_link_libraries(test_vector_clock coldchain_core)
target_include_directories(test_vector_clock PRIVATE third_party)
add_test(NAME VectorClock COMMAND test_vector_clock)

add_executable(test_causal_merge tests/test_causal_merge.cpp)
target_link_libraries(test_causal_merge coldchain_core)
target_include_directories(test_causal_merge PRIVATE third_party)
add_test(NAME CausalMerge COMMAND test_causal_merge)

