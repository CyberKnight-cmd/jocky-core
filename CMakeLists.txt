cmake_minimum_required(VERSION 3.20)
project(jocky CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Locate LLVM via llvm-config. Order of preference:
#   1) an explicit -DLLVM_CONFIG=... passed by the caller
#   2) a pinned copy vendored under third_party/llvm (see scripts/fetch-llvm.sh)
#   3) llvm-config found on PATH
# We deliberately avoid find_package (its exported targets reference a zstd
# static target not present on every host) and we do NOT adopt
# `llvm-config --cxxflags` wholesale, since that would force
# -fno-exceptions/-fno-rtti onto our own code (the parser uses exceptions).
set(LLVM_MIN_VERSION "23.0.0")
if(NOT DEFINED LLVM_CONFIG)
  set(_vendored "${CMAKE_SOURCE_DIR}/third_party/llvm/bin/llvm-config")
  if(EXISTS "${_vendored}")
    set(LLVM_CONFIG "${_vendored}")
  else()
    find_program(LLVM_CONFIG NAMES llvm-config llvm-config-23)
  endif()
endif()
if(NOT LLVM_CONFIG)
  message(FATAL_ERROR
    "llvm-config not found. Install LLVM ${LLVM_MIN_VERSION}+ (providing "
    "llvm-config on PATH), run scripts/fetch-llvm.sh to vendor a pinned copy, "
    "or pass -DLLVM_CONFIG=/path/to/llvm-config.")
endif()

execute_process(COMMAND ${LLVM_CONFIG} --version
  OUTPUT_VARIABLE LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(LLVM_VERSION VERSION_LESS LLVM_MIN_VERSION)
  message(FATAL_ERROR "LLVM ${LLVM_MIN_VERSION}+ required, found ${LLVM_VERSION} "
    "(from ${LLVM_CONFIG}).")
endif()
execute_process(COMMAND ${LLVM_CONFIG} --includedir
  OUTPUT_VARIABLE LLVM_INCLUDEDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libdir
  OUTPUT_VARIABLE LLVM_LIBDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core support native nativecodegen
  OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --system-libs
  OUTPUT_VARIABLE LLVM_SYSTEM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(LLVM_LIBS)
separate_arguments(LLVM_SYSTEM_LIBS)
# The prebuilt bakes in a Debian absolute path for static zstd that does not
# exist on this host; map any libzstd.a reference to the system shared lib.
list(TRANSFORM LLVM_SYSTEM_LIBS REPLACE ".*libzstd\\.a$" "-lzstd")
message(STATUS "Using LLVM ${LLVM_VERSION} (llvm-config: ${LLVM_CONFIG})")

add_executable(jockyc
  src/main.cpp
  # front end
  src/lexer.cpp
  src/token.cpp
  src/parser.cpp
  src/ast.cpp
  src/types.cpp
  # back end (code generation)
  src/codegen/driver.cpp
  src/codegen/lowering.cpp
  src/codegen/decls.cpp
  src/codegen/expr.cpp
  src/codegen/stmt.cpp
)
target_include_directories(jockyc PRIVATE src ${LLVM_INCLUDEDIR})
target_link_directories(jockyc PRIVATE ${LLVM_LIBDIR})
target_link_libraries(jockyc PRIVATE ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS})

# The JOCKY runtime, linked into every compiled program.
add_library(jocky_rt STATIC runtime/jocky_rt.c)
target_compile_options(jocky_rt PRIVATE -O2 -Wall -Wextra)

# Tell the driver, at configure time, where the runtime archive lives and
# which C compiler to shell out to for the final link.
target_compile_definitions(jockyc PRIVATE
  JOCKY_RT_LIB="$<TARGET_FILE:jocky_rt>"
  JOCKY_RT_INCLUDE="${CMAKE_SOURCE_DIR}/runtime"
  JOCKY_CC="${CMAKE_C_COMPILER}")
