# Concatenate the self-hosted-builtins prelude (src/runtime/prelude/*.ts, sorted)
# into a single .ts that ts-aot compiles with --prelude-object. Invoked at build
# time: cmake -DSRC_DIR=<prelude dir> -DOUT_FILE=<out.ts> -P concat_prelude.cmake
file(GLOB PRELUDE_FILES "${SRC_DIR}/*.ts")
list(SORT PRELUDE_FILES)
set(_OUT "")
foreach(_f ${PRELUDE_FILES})
    file(READ "${_f}" _c)
    string(APPEND _OUT "${_c}\n")
endforeach()
file(WRITE "${OUT_FILE}" "${_OUT}")
