cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0148)
  cmake_policy(SET CMP0148 OLD)
endif()
project(blobtemplates_sqlite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources are symlinked/copied into this directory from the parent project
set(BT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

include(FetchContent)

# ── Inja (header-only, pulls in nlohmann/json) ────────────────────
FetchContent_Declare(inja
  GIT_REPOSITORY https://github.com/pantor/inja.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE)
set(INJA_USE_EMBEDDED_JSON OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

# ── nlohmann/json ─────────────────────────────────────────────────
FetchContent_Declare(nlohmann_json
  URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ── jsoncons (header-only: JMESPath, JSON Patch, JSON Schema) ────
FetchContent_Declare(jsoncons
  GIT_REPOSITORY https://github.com/danielaparker/jsoncons.git
  GIT_TAG        v1.1.0
  GIT_SHALLOW    TRUE)
set(JSONCONS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ── dtl (diff template library, header-only) ────────────────────
FetchContent_Declare(dtl
  GIT_REPOSITORY https://github.com/cubicdaiya/dtl.git
  GIT_TAG        v1.21
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(dtl)

# ── rapidyaml (fast YAML parser with JSON emission) ─────────────
FetchContent_Declare(ryml
  GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
  GIT_TAG        v0.11.0
  GIT_SHALLOW    FALSE)

FetchContent_MakeAvailable(nlohmann_json inja jsoncons ryml)

set(BT_SOURCES ${BT_ROOT}/src/blobtemplates_core.cpp)
set(BT_INCLUDES ${BT_ROOT}/include ${dtl_SOURCE_DIR})
set(BT_LIBS pantor::inja nlohmann_json::nlohmann_json jsoncons ryml::ryml)

# ── SQLite headers ──────────────────────────────────────────────
FetchContent_Declare(sqlite3
  URL "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)

if(NOT TARGET sqlite3_lib)
  add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
  target_compile_definitions(sqlite3_lib PRIVATE SQLITE_CORE)
endif()

# ── SQLite extension ────────────────────────────────────────────
add_library(blobtemplates_sqlite MODULE
  ${BT_ROOT}/sqlite_ext/src/blobtemplates_sqlite.c ${BT_SOURCES})
target_include_directories(blobtemplates_sqlite PRIVATE
  ${BT_INCLUDES} ${sqlite3_SOURCE_DIR})
target_link_libraries(blobtemplates_sqlite PRIVATE ${BT_LIBS})
set_target_properties(blobtemplates_sqlite PROPERTIES
  OUTPUT_NAME "blobtemplates"
  PREFIX "")
if(APPLE)
  target_link_options(blobtemplates_sqlite PRIVATE -undefined dynamic_lookup)
endif()

# Install the .so/.dylib into the Python package directory
install(TARGETS blobtemplates_sqlite
  LIBRARY DESTINATION blobtemplates_sqlite)
# On macOS, also create the .dylib symlink
if(APPLE)
  install(CODE "
    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
      blobtemplates.so blobtemplates.dylib
      WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/blobtemplates_sqlite)
  ")
endif()
