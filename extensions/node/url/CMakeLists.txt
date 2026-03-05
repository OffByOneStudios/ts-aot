# url extension for ts-aot
#
# Node.js-compatible URL and querystring functions.
# Built as a separate library (ts_url) linked when url module is imported.

add_library(ts_url STATIC
    src/url.cpp
    src/querystring.cpp
)

target_include_directories(ts_url PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_url PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# ICU is required for domain name conversion (domainToASCII/domainToUnicode)
find_package(ICU REQUIRED COMPONENTS uc i18n data)

target_link_libraries(ts_url PRIVATE
    tsruntime
    ICU::uc
    ICU::i18n
    ICU::data
)
