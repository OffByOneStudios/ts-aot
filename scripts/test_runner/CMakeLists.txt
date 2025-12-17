cmake_minimum_required(VERSION 3.10)
project(TestRunner)

find_package(ICU COMPONENTS uc REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)

# TEST_OBJ is passed as an absolute path
# TSRUNTIME_LIB is passed as an absolute path

add_executable(test_app ${TEST_OBJ})
set_target_properties(test_app PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(test_app PRIVATE 
    ${TSRUNTIME_LIB}
    ICU::uc
    libuv::uv
    BDWgc::gc
)
