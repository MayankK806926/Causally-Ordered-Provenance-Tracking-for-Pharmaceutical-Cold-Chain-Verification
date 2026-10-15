cmake_minimum_required(VERSION 3.20)
project(coldchain_vclock CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Core library -----------------------------------------------------------
add_library(coldchain_core
    src/vector_clock.cpp
    src/matrix_clock.cpp
    src/events.cpp
    src/hashing.cpp
    src/ledger_block.cpp
    src/physical_clock.cpp
    src/causal_merge_engine.cpp
    src/iot_device.cpp
    src/ledger_node.cpp
    src/pruning_engine.cpp
    src/network_simulator.cpp
    src/metrics.cpp
)
target_include_directories(coldchain_core PUBLIC include)
target_include_directories(coldchain_core PRIVATE third_party)

# --- Simulation executable ---------------------------------------------------
add_executable(sim src/main_sim.cpp)
target_link_libraries(sim coldchain_core)
target_include_directories(sim PRIVATE third_party)

# --- Sweep runner (Week 5) ---------------------------------------------------
add_executable(sweep_runner experiments/sweep_runner.cpp)
target_link_libraries(sweep_runner coldchain_core)
target_include_directories(sweep_runner PRIVATE third_party)

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

add_executable(test_matrix_clock tests/test_matrix_clock.cpp)
target_link_libraries(test_matrix_clock coldchain_core)
target_include_directories(test_matrix_clock PRIVATE third_party)
add_test(NAME MatrixClock COMMAND test_matrix_clock)

add_executable(test_network_simulator tests/test_network_simulator.cpp)
target_link_libraries(test_network_simulator coldchain_core)
target_include_directories(test_network_simulator PRIVATE third_party)
add_test(NAME NetworkSimulator COMMAND test_network_simulator)

add_executable(test_pruning_safety tests/test_pruning_safety.cpp)
target_link_libraries(test_pruning_safety coldchain_core)
target_include_directories(test_pruning_safety PRIVATE third_party)
add_test(NAME PruningSafety COMMAND test_pruning_safety)
