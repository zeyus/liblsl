#SET(CMAKE_SYSTEM_NAME Linux)
#SET(CMAKE_SYSTEM_VERSION 1)
#SET(CMAKE_C_COMPILER $ENV{PITOOLS}/arm-bcm2708/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc)
#SET(CMAKE_CXX_COMPILER $ENV{PITOOLS}/arm-bcm2708/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc)
#SET(CMAKE_FIND_ROOT_PATH $ENV{PITOOLS}/arm-bcm2708/arm-linux-gnueabihf/arm-linux-gnueabihf/sysroot/)
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
add_definitions(-Wall)
