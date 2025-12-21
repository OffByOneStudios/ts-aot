cmake_minimum_required(VERSION 3.10)
project(TestRunner)

find_package(ICU COMPONENTS uc i18n REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# TEST_OBJ is passed as an absolute path
# TSRUNTIME_LIB is passed as an absolute path

add_executable(test_app main_wrapper.cpp)
target_sources(test_app PRIVATE ${TEST_OBJ})

target_link_libraries(test_app PRIVATE 
    ${TSRUNTIME_LIB}
    ICU::uc
    ICU::i18n
    libuv::uv
    BDWgc::gc
    nlohmann_json::nlohmann_json
    llhttp::llhttp_static
    OpenSSL::SSL
    OpenSSL::Crypto
)
