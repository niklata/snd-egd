project (sndegd)

cmake_minimum_required (VERSION 2.6)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -std=gnu99 -pedantic -Wall -lcap -lasound -lm")

file(GLOB SNDEGD_SRCS "*.c")

add_executable(snd-egd ${SNDEGD_SRCS})
