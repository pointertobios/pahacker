cmake_minimum_required(VERSION 3.18)
project(phad_hacker)

set(SRCP ./src)

set(PAH_PATH )
set(PAH_PATH ${PAH_PATH} ${SRCP}/pahacker/main.c)
set(PAH_PATH ${PAH_PATH} ${SRCP}/pahacker/vad2pad.c)
set(PAH_PATH ${PAH_PATH} ${SRCP}/pahacker/memread.c)

set(PAHD_PATH )
set(PAHD_PATH ${PAHD_PATH} ${SRCP}/pahdump/dumpf.c)

link_directories(build)

add_executable(pahacker ${PAH_PATH})
target_link_libraries(pahacker libpthread.so libpahdump.so)

add_library(pahdump SHARED ${PAHD_PATH})
