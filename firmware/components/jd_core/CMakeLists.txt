# Pure C diagnostic core: no ESP-IDF dependencies, tested on the Mac (test/host).
file(GLOB JD_CORE_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(SRCS ${JD_CORE_SRCS} INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
