find_package(Catch2 CONFIG REQUIRED)

add_executable(unit_tests
    unit/RuntimeTests.cpp
)

target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    tsruntime
)

# Include runtime headers
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# Register tests
include(Catch)
catch_discover_tests(unit_tests)
