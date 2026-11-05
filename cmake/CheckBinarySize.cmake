if(NOT CONFIGURATION STREQUAL "Release")
    return()
endif()

if(NOT DEFINED BINARY_PATH OR NOT EXISTS "${BINARY_PATH}")
    message(FATAL_ERROR "Release binary does not exist: ${BINARY_PATH}")
endif()

if(NOT DEFINED MAX_SIZE_BYTES)
    message(FATAL_ERROR "MAX_SIZE_BYTES must be provided")
endif()

file(SIZE "${BINARY_PATH}" BINARY_SIZE_BYTES)
if(BINARY_SIZE_BYTES GREATER MAX_SIZE_BYTES)
    message(FATAL_ERROR
        "Release binary is ${BINARY_SIZE_BYTES} bytes, "
        "over the ${MAX_SIZE_BYTES}-byte limit: ${BINARY_PATH}")
endif()

message(STATUS
    "Stripped release binary size: ${BINARY_SIZE_BYTES} bytes "
    "(limit ${MAX_SIZE_BYTES} bytes)")
