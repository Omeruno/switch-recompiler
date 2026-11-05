include(${WAMR_ROOT_DIR}/core/shared/platform/linux/shared_platform.cmake)
# Exercise the same reservation/commit policy used by the libnx port.
list(FILTER PLATFORM_SHARED_SOURCE EXCLUDE REGEX "/posix_memmap\\.c$")
list(APPEND PLATFORM_SHARED_SOURCE
  ${CMAKE_CURRENT_LIST_DIR}/memory_backend.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/memory_port.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/linear_memory.c)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../src)
