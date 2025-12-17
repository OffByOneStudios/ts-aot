find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc REQUIRED)

add_library(tsruntime STATIC
    src/Core.cpp
    src/Memory.cpp
    src/EventLoop.cpp
    src/TsString.cpp
    src/TsArray.cpp
    src/IO.cpp
    src/Primitives.cpp
    src/Math.cpp
    # Add other source files here as they are created
)

target_include_directories(tsruntime PUBLIC include)

target_link_libraries(tsruntime PUBLIC
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    BDWgc::gc
    ICU::uc
)

target_compile_definitions(tsruntime PUBLIC TS_RUNTIME_EXPORT)
