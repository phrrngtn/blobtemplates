cmake_minimum_required(VERSION 3.20)
project(blobtemplates LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

FetchContent_MakeAvailable(nlohmann_json inja)

# ── Core sources (compiled into each target) ──────────────────────
set(BT_SOURCES src/blobtemplates_core.cpp)
set(BT_INCLUDES include)
set(BT_LIBS pantor::inja nlohmann_json::nlohmann_json)

# ── C example ─────────────────────────────────────────────────────
add_executable(blobtemplates_example example/main.c ${BT_SOURCES})
target_include_directories(blobtemplates_example PRIVATE ${BT_INCLUDES})
target_link_libraries(blobtemplates_example PRIVATE ${BT_LIBS})

# ── Python bindings (nanobind) ─────────────────────────────────────
option(BUILD_PYTHON_BINDINGS "Build nanobind Python module" OFF)
if(BUILD_PYTHON_BINDINGS)
  find_package(Python 3.8 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
  find_package(nanobind CONFIG REQUIRED)

  nanobind_add_module(blobtemplates_ext NB_STATIC python/bindings.cpp ${BT_SOURCES})
  target_include_directories(blobtemplates_ext PRIVATE ${BT_INCLUDES})
  target_link_libraries(blobtemplates_ext PRIVATE ${BT_LIBS})
  set_target_properties(blobtemplates_ext PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/blobtemplates")
endif()

# ── DuckDB C API headers (for extension build) ────────────────────
FetchContent_Declare(duckdb_capi
  GIT_REPOSITORY https://github.com/duckdb/extension-template-c.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE)
FetchContent_Populate(duckdb_capi)
set(DUCKDB_CAPI_INCLUDE "${duckdb_capi_SOURCE_DIR}/duckdb_capi")

# ── DuckDB extension ──────────────────────────────────────────────
option(BUILD_DUCKDB_EXTENSION "Build DuckDB loadable extension" OFF)
if(BUILD_DUCKDB_EXTENSION)
  add_library(blobtemplates_duckdb MODULE
    duckdb_ext/src/blobtemplates_duckdb.c ${BT_SOURCES})
  target_include_directories(blobtemplates_duckdb PRIVATE
    ${BT_INCLUDES} ${DUCKDB_CAPI_INCLUDE})
  target_link_libraries(blobtemplates_duckdb PRIVATE ${BT_LIBS})
  set_target_properties(blobtemplates_duckdb PROPERTIES
    OUTPUT_NAME "blobtemplates"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/duckdb")
  if(APPLE)
    target_link_options(blobtemplates_duckdb PRIVATE -undefined dynamic_lookup)
  endif()

  # Append DuckDB extension metadata
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_custom_command(TARGET blobtemplates_duckdb POST_BUILD
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/duckdb_ext/append_metadata.py"
        "$<TARGET_FILE:blobtemplates_duckdb>"
        "${CMAKE_BINARY_DIR}/duckdb/blobtemplates.duckdb_extension"
      COMMENT "Appending DuckDB extension metadata")
  endif()
endif()

# ── SQLite extension ──────────────────────────────────────────────
option(BUILD_SQLITE_EXTENSION "Build SQLite loadable extension" OFF)
if(BUILD_SQLITE_EXTENSION)
  FetchContent_Declare(sqlite3
    URL "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(sqlite3)

  if(NOT TARGET sqlite3_lib)
    add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
    target_compile_definitions(sqlite3_lib PRIVATE SQLITE_CORE)
  endif()

  add_library(blobtemplates_sqlite MODULE
    sqlite_ext/src/blobtemplates_sqlite.c ${BT_SOURCES})
  target_include_directories(blobtemplates_sqlite PRIVATE
    ${BT_INCLUDES} ${sqlite3_SOURCE_DIR})
  target_link_libraries(blobtemplates_sqlite PRIVATE ${BT_LIBS})
  set_target_properties(blobtemplates_sqlite PROPERTIES
    OUTPUT_NAME "blobtemplates"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sqlite")
  if(APPLE)
    target_link_options(blobtemplates_sqlite PRIVATE -undefined dynamic_lookup)
    add_custom_command(TARGET blobtemplates_sqlite POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        blobtemplates.so "$<TARGET_FILE_DIR:blobtemplates_sqlite>/blobtemplates.dylib")
  endif()
endif()
