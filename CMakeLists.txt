cmake_minimum_required(VERSION 3.10)

# set the project name
project(ff)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# Fix warnings: https://stackoverflow.com/questions/48149323/what-does-the-gcc-warning-project-parameter-passing-for-x-changed-in-gcc-7-1-m
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" " -Wno-psabi")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


# Set variables for specific boards
message("-- CMAKE_SYSTEM_INFO_FILE: ${CMAKE_SYSTEM_INFO_FILE}")
message("-- CMAKE_SYSTEM_NAME:      ${CMAKE_SYSTEM_NAME}")
message("-- CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message("-- CMAKE_SYSTEM:           ${CMAKE_SYSTEM}")

file(READ "/sys/firmware/devicetree/base/model" MODEL)
message("-- MODEL:                  ${MODEL}")


# Libraries

# This for native RPI
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

set(rpi_extra_sources
    src/gcs.cpp
    src/mailbox.cpp
    src/qpu_base.cpp
    src/qpu_info.cpp
    src/qpu_program.cpp
)
include_directories(/usr/include/libdrm)


find_package(OpenCV REQUIRED)


# QPU files for RPI    

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
add_custom_target(qpu_programs)
foreach (file ${QPU_SOURCES})
    get_filename_component(file_name ${file} NAME_WE)
    add_custom_command(
        OUTPUT ${file_name}.o
        DEPENDS ${file}
        COMMAND ${VC4ASM} -V -o ${CMAKE_CURRENT_BINARY_DIR}/${file_name}.bin ${CMAKE_SOURCE_DIR}/${file}
        #COMMAND objcopy -I binary -O elf32-littlearm --rename-section .data=.rodata,alloc,load,readonly ${file_name}.bin ${file_name}.o
        COMMAND ${TOOLCHAIN_LD} -r -b binary -o ${file_name}.o ${file_name}.bin 
    )
    add_custom_target(${file_name}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${file_name}.o
    )
    add_dependencies(qpu_programs ${file_name})
    list(APPEND QPU_OBJECTS ${file_name}.o)
endforeach()

set_source_files_properties(qpu_warp.o PROPERTIES EXTERNAL_OBJECT true GENERATED true)


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
add_custom_target(vpu_functions
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/vpu_functions.o
)


# add the executable
add_executable(
    ff 
    src/main.cpp
    src/detector.cpp
    src/glhelpers.cpp
    src/stb.cpp
    src/tui.cpp
    ${rpi_extra_sources}

    ${QPU_OBJECTS}
    vpu_functions.o    
)
add_dependencies(ff qpu_programs vpu_functions)


# includes
target_include_directories(
    ff
    PUBLIC ${OPENGL_INCLUDE_DIR}
    PUBLIC ${GLFW_INCLUDE_DIR}
    #PUBLIC ${GLEW_INCLUDE_DIRS}
)

target_link_libraries(
    ff
    pthread
    curses
    ${OPENGL_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${OpenCV_LIBS}
    
)



# add the executable
add_executable(
    small
    src/small.cpp
    src/detector.cpp
    src/glhelpers.cpp
    src/stb.cpp
    src/tui.cpp

    ${rpi_extra_sources}
    ${QPU_OBJECTS}
    vpu_functions.o    
)

# includes
target_include_directories(
    small
    PUBLIC ${OPENGL_INCLUDE_DIR}
    PUBLIC ${GLFW_INCLUDE_DIR}
)

target_link_libraries(
    small
    pthread
    curses
    ${OPENGL_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${OpenCV_LIBS}
    
)

# add the executable
add_executable(
    min_detect
    src/min_detect.cpp
    src/detector.cpp
    src/glhelpers.cpp
    src/stb.cpp
    src/tui.cpp

    ${rpi_extra_sources}
    ${QPU_OBJECTS}
    vpu_functions.o    
)

# includes
target_include_directories(
    min_detect
    PUBLIC ${OPENGL_INCLUDE_DIR}
    PUBLIC ${GLFW_INCLUDE_DIR}
)

target_link_libraries(
    min_detect
    pthread
    curses
    ${OPENGL_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${OpenCV_LIBS}
    
)
