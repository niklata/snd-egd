project (sndegd)

cmake_minimum_required (VERSION 2.6)

find_package(ALSA)
link_directories(${ALSA_LIBRARIES})
include_directories(${ALSA_INCLUDE_DIRS})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -std=gnu99 -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -lm")

if (WIN32)
  set(OSNAME "Win32")
else()
execute_process(
  COMMAND uname
  COMMAND tr "\n" " "
  COMMAND sed "s/ //"
  OUTPUT_VARIABLE OSNAME
  )
endif()

if (NOT (${OSNAME} STREQUAL "Linux"))
  message("ndhc requires Linux.  Patches are welcome.  Consult README.")
  return()
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNK_USE_CAPABILITY")

include_directories("${PROJECT_SOURCE_DIR}/ncmlib")
add_subdirectory(ncmlib)

file(GLOB SNDEGD_SRCS "*.c")

add_executable(snd-egd ${SNDEGD_SRCS})
target_link_libraries(snd-egd ncmlib ${ALSA_LIBRARIES})
