cmake_minimum_required(VERSION 3.10)

# set the project name
project(frappe VERSION 0.1)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# Fix warnings: https://stackoverflow.com/questions/48149323/what-does-the-gcc-warning-project-parameter-passing-for-x-changed-in-gcc-7-1-m
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")


# Set variables for specific boards
message("-- CMAKE_SYSTEM_INFO_FILE: ${CMAKE_SYSTEM_INFO_FILE}")
message("-- CMAKE_SYSTEM_NAME:      ${CMAKE_SYSTEM_NAME}")
message("-- CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message("-- CMAKE_SYSTEM:           ${CMAKE_SYSTEM}")
message("-- CMAKE_BUILD_TYPE:       ${CMAKE_BUILD_TYPE}")

file(READ "/sys/firmware/devicetree/base/model" MODEL)
message("-- MODEL:                  ${MODEL}")


# -----------------------------------------------------------------------
# Native RPI legacy libraries and dependencies
include_directories(${CMAKE_SYSROOT}/usr/local/include)
include_directories(${CMAKE_SYSROOT}/opt/vc/include/)
include_directories(${CMAKE_SYSROOT}/opt/vc/include/interface/vcos/pthreads)
include_directories(${CMAKE_SYSROOT}/opt/vc/include/interface/vmcs_host/linux)
include_directories(${CMAKE_SYSROOT}/opt/vc/include/interface/vcsm)
include_directories(shaders)
find_library(LIB_BCMH NAMES bcm_host HINTS /opt/vc/lib/)
find_library(LIB_VCOS NAMES vcos HINTS /opt/vc/lib/)
find_library(LIB_VCSM NAMES vcsm HINTS /opt/vc/lib/)
find_library(LIB_MMAL NAMES mmal HINTS /opt/vc/lib/)
find_library(LIB_MMAL_CORE NAMES mmal_core HINTS /opt/vc/lib/)
find_library(LIB_MMAL_UTIL NAMES mmal_util HINTS /opt/vc/lib/)
find_library(LIB_MMAL_COMP NAMES mmal_components HINTS /opt/vc/lib/)
find_library(LIB_BEGL NAMES brcmEGL HINTS /opt/vc/lib/)
find_library(LIB_GLES NAMES brcmGLESv2 HINTS /opt/vc/lib/)
set(OPENGL_LIBRARIES
    ${LIB_BCMH} ${LIB_VCOS} ${LIB_VCSM}
    ${LIB_MMAL} ${LIB_MMAL_CORE} ${LIB_MMAL_UTIL} ${LIB_MMAL_COMP}
    ${LIB_BEGL} 
    ${LIB_GLES})

find_package(OpenCV REQUIRED)

set(TARGET_LIBRARIES
    pthread
    curses
    ${OPENGL_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${OpenCV_LIBS} 
)


# -----------------------------------------------------------------------
# Main sources
set(FRAPPE_SOURCES
    src/detector.cpp
    src/glhelpers.cpp
    src/stb.cpp
    src/tui.cpp
    src/gcs.cpp
    src/mailbox.cpp
    src/qpu_base.cpp
    src/qpu_info.cpp
    src/qpu_program.cpp
)

# -----------------------------------------------------------------------
# Custom build for the QPU shaders

# Set VC4ASM compiler to compile all asm source files
if(NOT CMAKE_CROSSCOMPILING)
    set(VC4ASM "vc4asm")
    set(VC4_TOOLPATH "")
endif()
set(QPU_SOURCES
    shaders/qpu_shi_tomasi_opt_tiled.asm
    shaders/qpu_shi_tomasi_scale_tiled.asm
    shaders/qpu_suppress_opt_tiled.asm
    shaders/qpu_warp.asm
)
set(QPU_OBJECTS "")
foreach (file ${QPU_SOURCES})
    get_filename_component(file_name ${file} NAME_WE)
    add_custom_command(
        OUTPUT ${file_name}.o
        DEPENDS ${file}
        COMMAND ${VC4ASM} -V -o ${CMAKE_CURRENT_BINARY_DIR}/${file_name}.bin ${CMAKE_SOURCE_DIR}/${file}
        COMMAND ${TOOLCHAIN_LD} -r -b binary -o ${file_name}.o ${file_name}.bin 
    )
    add_custom_target(${file_name}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${file_name}.o
    )
    list(APPEND QPU_OBJECTS ${file_name}.o)
endforeach()

# -----------------------------------------------------------------------
# Custom build for the VPU functions
set(VPU_SOURCES
    ${CMAKE_SOURCE_DIR}/vc4/crt0.S
    ${CMAKE_SOURCE_DIR}/vc4/crti.S
    ${CMAKE_SOURCE_DIR}/vc4/crtn.S
    ${CMAKE_SOURCE_DIR}/vc4/helpers.S
    ${CMAKE_SOURCE_DIR}/vc4/vpu_functions.c
)

add_custom_command(
    OUTPUT vpu_functions.o
    DEPENDS ${VPU_SOURCES}
    COMMAND ${VC4_TOOLPATH}/vc4-elf-gcc -o ${CMAKE_CURRENT_BINARY_DIR}/vpu_functions.elf -O3 -T vc4-sim.ld -nostartfiles -fPIC -static ${VPU_SOURCES}
    COMMAND ${VC4_TOOLPATH}/vc4-elf-objcopy -O binary vpu_functions.elf vpu_functions.bin
    COMMAND ${TOOLCHAIN_LD} -r -b binary -o vpu_functions.o vpu_functions.bin 
)
add_custom_target(vpu_functions DEPENDS vpu_functions.o)

# -----------------------------------------------------------------------
# Shared library
add_library(frappe SHARED
    ${FRAPPE_SOURCES}
)
set_target_properties(frappe PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(frappe PROPERTIES PUBLIC_HEADER include/frappe.hpp)
target_include_directories(frappe PUBLIC ${OPENGL_INCLUDE_DIR})
target_link_libraries(frappe ${TARGET_LIBRARIES})

# -----------------------------------------------------------------------
# Executables
# ff - text UI interactive
add_executable(
    ff 
    src/main.cpp
    ${FRAPPE_SOURCES}
    ${QPU_OBJECTS}
    vpu_functions.o
)
add_dependencies(ff vpu_functions)

# small - benchmarking app
add_executable(
    small
    src/small.cpp
    ${FRAPPE_SOURCES}
    ${QPU_OBJECTS}
    vpu_functions.o
)
add_dependencies(small vpu_functions)

# min_detect - minimal example
add_executable(
    min_detect
    src/min_detect.cpp
    ${FRAPPE_SOURCES}
    ${QPU_OBJECTS}
    vpu_functions.o
)
add_dependencies(min_detect vpu_functions)

# min_detect - minimal example
add_executable(
    stream
    src/stream.cpp
    ${FRAPPE_SOURCES}
    ${QPU_OBJECTS}
    vpu_functions.o
)
add_dependencies(stream vpu_functions)

target_include_directories(ff PUBLIC ${OPENGL_INCLUDE_DIR})
target_include_directories(small PUBLIC ${OPENGL_INCLUDE_DIR})
target_include_directories(min_detect PUBLIC ${OPENGL_INCLUDE_DIR})
target_include_directories(stream PUBLIC ${OPENGL_INCLUDE_DIR})

target_link_libraries(ff ${TARGET_LIBRARIES})
target_link_libraries(small ${TARGET_LIBRARIES})
target_link_libraries(min_detect ${TARGET_LIBRARIES})
target_link_libraries(stream ${TARGET_LIBRARIES})
