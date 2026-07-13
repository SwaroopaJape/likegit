cmake_minimum_required ( VERSION 3.14)
project ( likegit CXX)
set( CMAKE_CXX_STANDARD 17)
set( CMAKE_CXX_STANDARD_REQUIRED ON)


# 2. Build our Core Library
# We put our logic in a library so we can test it independently of the CLI
add_library ( likegit_core
src/core.cpp
src/sha1.cpp
)
# Tell the library where our header files (. hpp) live
target_include_directories ( likegit_core PUBLIC include )


# 3. Build the Command Line Executable
add_executable ( likegit src/main.cpp)
# Link our core library TO our executable
target_link_libraries ( likegit PRIVATE likegit_core )
# --- CLASSROOM EXPLANATION : PUBLIC vs PRIVATE ---
# Notice we use 'PRIVATE ' here. This tells CMake: "The executable needs the
# library to run , but it doesn 't pass the library 's dependencies on to anyone else."
# In large systems , managing these visibility scopes prevents dependency hell!
# ------------------------------------------------


# 4. Fetch and Link External Libraries (Zlib only — SHA-1 is implemented manually)
find_package (ZLIB REQUIRED )
target_link_libraries ( likegit_core PRIVATE ZLIB::ZLIB)


# 5. Testing Setup ( GoogleTest )
enable_testing ()
include ( FetchContent )
FetchContent_Declare (
googletest
URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
# Set extracted file timestamps to the time of extraction (resolves warning about policy CMP0135)
DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable ( googletest )

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable( nlohmann_json )
# Link nlohmann as PUBLIC so downstream targets (tests, executable) inherit the include paths
target_link_libraries ( likegit_core PUBLIC nlohmann_json::nlohmann_json)
add_executable ( likegit_tests tests/test_core.cpp)
target_link_libraries ( likegit_tests PRIVATE likegit_core gtest_main nlohmann_json::nlohmann_json ZLIB::ZLIB)
include ( GoogleTest )
gtest_discover_tests ( likegit_tests )