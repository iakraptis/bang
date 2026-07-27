cmake_minimum_required(VERSION 3.24)

project(bang_duel_agent LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BANG_BUILD_TESTS "Build native tests" ON)
option(BANG_ENABLE_TORCH "Build LibTorch policy and training commands" OFF)

set(BANG_WARNING_OPTIONS -Wall -Wextra -Wpedantic)

add_library(bang_core
  src/action_space.cpp
  src/game.cpp
  src/heuristic.cpp
  src/json.cpp
  src/static_data.cpp
  src/static_policy.cpp
  src/training_state.cpp
  src/web.cpp
)
target_include_directories(bang_core PUBLIC include)
target_compile_options(bang_core PRIVATE ${BANG_WARNING_OPTIONS})

if(BANG_ENABLE_TORCH)
  find_package(Torch REQUIRED)
  separate_arguments(TORCH_CXX_FLAGS_LIST NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
  target_sources(bang_core PRIVATE
    src/checkpoint.cpp
    src/policy_network.cpp
    src/training.cpp
  )
  target_compile_definitions(bang_core PUBLIC BANG_ENABLE_TORCH=1)
  target_compile_options(bang_core PUBLIC ${TORCH_CXX_FLAGS_LIST})
  target_link_libraries(bang_core PUBLIC "${TORCH_LIBRARIES}")
endif()

add_executable(bang_duel src/main.cpp)
target_link_libraries(bang_duel PRIVATE bang_core)
target_compile_options(bang_duel PRIVATE ${BANG_WARNING_OPTIONS})

if(BANG_BUILD_TESTS)
  enable_testing()
  add_executable(bang_tests tests/native_tests.cpp)
  target_link_libraries(bang_tests PRIVATE bang_core)
  target_compile_options(bang_tests PRIVATE ${BANG_WARNING_OPTIONS})
  add_test(NAME bang_tests COMMAND bang_tests)
  find_program(BANG_NODE_EXECUTABLE node)
  if(BANG_NODE_EXECUTABLE)
    add_test(
      NAME web_targeting_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_targeting_tests.js"
    )
    add_test(
      NAME web_card_fx_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_card_fx_tests.js"
    )
    add_test(
      NAME web_history_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_history_tests.js"
    )
    add_test(
      NAME web_card_back_assets_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_card_back_assets_tests.js"
    )
    add_test(
      NAME web_static_engine_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_static_engine_tests.js"
    )
    add_test(
      NAME web_voxel_models_tests
      COMMAND "${BANG_NODE_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_voxel_models_tests.js"
    )
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME about_data_tests
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/about_data_tests.py"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()
  if(BANG_ENABLE_TORCH)
    if(NOT Python3_Interpreter_FOUND)
      message(FATAL_ERROR "Python 3 is required for training tests")
    endif()
    add_test(
      NAME training_cli_tests
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/train_cli_tests.py"
        "$<TARGET_FILE:bang_duel>"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()
endif()
