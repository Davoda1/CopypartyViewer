# Loaded after project() in the private CMake dependencies.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build>")
endif()
