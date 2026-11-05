set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})
add_definitions(-DBH_PLATFORM_SWITCH)
include_directories(${PLATFORM_SHARED_DIR} ${SHARED_DIR}/platform/include ${CMAKE_CURRENT_LIST_DIR}/../../src)
set(PLATFORM_SHARED_SOURCE ${PLATFORM_SHARED_DIR}/platform.c
  ${CMAKE_CURRENT_LIST_DIR}/memory_backend.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/memory_port.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/linear_memory.c)
