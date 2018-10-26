cmake_minimum_required(VERSION 3.3)
if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
  cmake_policy(VERSION 3.12)
endif()

project(snd-egd DESCRIPTION "gathers entropy from sound device and feeds to /dev/random"
                LANGUAGES C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -Wsign-conversion -DNK_USE_CAPABILITY")

find_package(ALSA)

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

add_subdirectory(ncmlib)

add_executable(snd-egd)
target_sources(snd-egd PRIVATE
  "alsa.c"
  "getrandom.c"
  "rb.c"
  "snd-egd.c"
)
target_include_directories(snd-egd PRIVATE "${PROJECT_SOURCE_DIR}/ncmlib" ${ALSA_INCLUDE_DIRS})
target_link_libraries(snd-egd PUBLIC ncmlib ${ALSA_LIBRARIES})
