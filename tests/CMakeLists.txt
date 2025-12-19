find_package(Catch2 CONFIG REQUIRED)

add_executable(unit_tests
    unit/RuntimeTests.cpp
    unit/TsStringTests.cpp
    unit/TsArrayTests.cpp
    unit/TsDateTest.cpp
    unit/AnalyzerTests.cpp
)

target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    tsruntime
    tscompiler_lib
)

# Include runtime and compiler headers
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/compiler
)

# Register tests
include(Catch)
catch_discover_tests(unit_tests)
