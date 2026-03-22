cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0148)
  cmake_policy(SET CMP0148 OLD)
endif()
project(blobtemplates_duckdb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# ── DuckDB C API headers ────────────────────────────────────────
FetchContent_Declare(duckdb_capi
  GIT_REPOSITORY https://github.com/duckdb/extension-template-c.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE)
FetchContent_Populate(duckdb_capi)
set(DUCKDB_CAPI_INCLUDE "${duckdb_capi_SOURCE_DIR}/duckdb_capi")

# ── DuckDB extension ────────────────────────────────────────────
add_library(blobtemplates_duckdb MODULE
  ${BT_ROOT}/duckdb_ext/src/blobtemplates_duckdb.c ${BT_SOURCES})
target_include_directories(blobtemplates_duckdb PRIVATE
  ${BT_INCLUDES} ${DUCKDB_CAPI_INCLUDE})
target_link_libraries(blobtemplates_duckdb PRIVATE ${BT_LIBS})
set_target_properties(blobtemplates_duckdb PROPERTIES
  OUTPUT_NAME "blobtemplates"
  PREFIX "")
if(APPLE)
  target_link_options(blobtemplates_duckdb PRIVATE -undefined dynamic_lookup)
endif()

# Append DuckDB extension metadata
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_custom_command(TARGET blobtemplates_duckdb POST_BUILD
    COMMAND ${Python3_EXECUTABLE}
      "${BT_ROOT}/duckdb_ext/append_metadata.py"
      "$<TARGET_FILE:blobtemplates_duckdb>"
      "$<TARGET_FILE_DIR:blobtemplates_duckdb>/blobtemplates.duckdb_extension"
    COMMENT "Appending DuckDB extension metadata")
endif()

# Install the .duckdb_extension into the Python package directory
install(FILES "$<TARGET_FILE_DIR:blobtemplates_duckdb>/blobtemplates.duckdb_extension"
  DESTINATION blobtemplates_duckdb)
