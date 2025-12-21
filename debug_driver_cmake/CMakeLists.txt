cmake_minimum_required(VERSION 3.15)
project(debug_driver)

set(CMAKE_CXX_STANDARD 20)

find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(debug_driver debug_driver.cpp)

# Link against the generated object file
target_sources(debug_driver PRIVATE ${TEST_OBJ})

target_link_libraries(debug_driver PRIVATE 
    ${TSRUNTIME_LIB}
    BDWgc::gc
    ICU::uc
    ICU::i18n
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    llhttp::llhttp_static
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(debug_driver PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)
