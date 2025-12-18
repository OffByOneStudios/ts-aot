find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(tsruntime STATIC
    src/Core.cpp
    src/Memory.cpp
    src/EventLoop.cpp
    src/TsString.cpp
    src/TsArray.cpp
    src/IO.cpp
    src/Primitives.cpp
    src/Math.cpp
    src/TsMap.cpp
    src/TsObject.cpp
    src/Crypto.cpp
    src/md5.cpp
    src/TsJSON.cpp
    src/TsDate.cpp
    src/TsRegExp.cpp
)

target_include_directories(tsruntime PUBLIC include)

target_link_libraries(tsruntime PUBLIC
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    BDWgc::gc
    ICU::uc
    ICU::i18n
    unofficial-sodium::sodium
    nlohmann_json::nlohmann_json
)

target_compile_definitions(tsruntime PUBLIC TS_RUNTIME_EXPORT)
