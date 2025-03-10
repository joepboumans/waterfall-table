# This CMake code is partially based on: https://github.com/COMSYS/pie-for-tofino

set(CMAKE_CXX_STANDARD 17)
set(SDE_LIB_PATH $ENV{SDE_INSTALL}/lib)
set(THREADS_PREFER_PTHREAD_FLAG ON)

include(GNUInstallDirs)
include_directories($ENV{SDE_INSTALL}/include/)

find_package(Threads REQUIRED)

set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} /$SDE_INSTALL/lib)

find_library(BF_SWITCHD_LIBRARY libbf_switch.so PATHS ${SDE_LIB_PATH})
find_library(DRIVER_LIBRARY libdriver.so PATHS ${SDE_LIB_PATH})
find_library(BFUTILS_LIBRARY libbfutils.so PATHS ${SDE_LIB_PATH})
find_library(TARGET_SYS_LIBRARY libtarget_sys.so PATHS ${SDE_LIB_PATH})

# Optimization flags...
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og -flto -march=native -ftree-vectorize")

add_executable(main main.cpp ControlPlane.cpp simple_digest.cpp)
target_link_libraries(main Threads::Threads)

target_link_libraries(main
    ${BF_SWITCHD_LIBRARY} 
    ${DRIVER_LIBRARY} 
    ${BFUTILS_LIBRARY}
    ${TARGET_SYS_LIBRARY}
)
