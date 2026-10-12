cmake_minimum_required(VERSION 3.16)
project(sparky86 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_BOOT "Build the boot image (pmode_core.bin)" ON)
option(BUILD_SIM  "Build the simulator (sparky86)" ON)

set(BOOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/boot")
set(SIM_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src/sim")

# ---------------------------------------------------------------- boot
if(BUILD_BOOT)
    find_program(FASM_EXECUTABLE fasm REQUIRED)

    file(GLOB BOOT_ASM_FILES "${BOOT_DIR}/*.asm")
    set(BOOT_OBJ    "${BOOT_DIR}/boot.o")
    # C++ units compiled into the guest image
    set(BOOT_UNITS payload gpu input)
    file(GLOB BOOT_HEADERS "${BOOT_DIR}/*.h")
    set(BOOT_CXX_FLAGS -m32 -march=i386 -ffreestanding -O2 -fno-pic -fno-pie -fno-stack-protector
                         -fno-exceptions -fno-rtti -mno-80387 -mno-mmx -mno-sse)
    set(BOOT_BIN    "${CMAKE_CURRENT_SOURCE_DIR}/pmode_core.bin")

    add_custom_command(
        OUTPUT "${BOOT_OBJ}"
        COMMAND ${FASM_EXECUTABLE} boot.asm "${BOOT_OBJ}"
        WORKING_DIRECTORY "${BOOT_DIR}"
        DEPENDS ${BOOT_ASM_FILES}
        COMMENT "fasm boot.asm"
        VERBATIM)

    set(BOOT_UNIT_OBJS "")
    set(BOOT_UNIT_OBJ_NAMES "")
    foreach(unit ${BOOT_UNITS})
        set(unit_obj "${BOOT_DIR}/${unit}.o")
        add_custom_command(
            OUTPUT "${unit_obj}"
            COMMAND ${CMAKE_CXX_COMPILER} ${BOOT_CXX_FLAGS}
                    -c "${BOOT_DIR}/${unit}.cpp" -o "${unit_obj}"
            DEPENDS "${BOOT_DIR}/${unit}.cpp" ${BOOT_HEADERS}
            COMMENT "Compiling ${unit}.cpp (freestanding i386)"
            VERBATIM)
        list(APPEND BOOT_UNIT_OBJS "${unit_obj}")
        list(APPEND BOOT_UNIT_OBJ_NAMES "${unit}.o")
    endforeach()

    add_custom_command(
        OUTPUT "${BOOT_BIN}"
        COMMAND ${CMAKE_LINKER} -m elf_i386 -T linker.ld
                boot.o ${BOOT_UNIT_OBJ_NAMES} -o "${BOOT_BIN}"
        WORKING_DIRECTORY "${BOOT_DIR}"
        DEPENDS "${BOOT_OBJ}" ${BOOT_UNIT_OBJS} "${BOOT_DIR}/linker.ld"
        COMMENT "Linking pmode_core.bin"
        VERBATIM)

    add_custom_target(boot ALL DEPENDS "${BOOT_BIN}")
endif()

# ---------------------------------------------------------------- sim
if(BUILD_SIM)
    set(SIMLIB_PREFIX "$ENV{HOME}/opt" CACHE PATH "Install prefix of simlib")
    set(SIMLIB_INCLUDE_DIR "${SIMLIB_PREFIX}/include")
    set(SIMLIB_LIBRARY_DIR "${SIMLIB_PREFIX}/lib")
    set(SIMLIB_IMGUI_INCLUDE_DIR "${SIMLIB_INCLUDE_DIR}"
        CACHE PATH "Directory containing imgui.h")

    if(NOT EXISTS "${SIMLIB_IMGUI_INCLUDE_DIR}/imgui.h")
        message(FATAL_ERROR
            "Could not find imgui.h. Set SIMLIB_IMGUI_INCLUDE_DIR to the fetched ImGui source directory.")
    endif()

    find_package(OpenGL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(SQLite3 REQUIRED)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    find_package(Threads REQUIRED)
    find_package(SDL2_ttf QUIET CONFIG)

    if(TARGET SDL2_ttf::SDL2_ttf)
        set(SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf)
    elseif(TARGET SDL2_ttf::SDL2_ttf-static)
        set(SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf-static)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
        set(SDL2_TTF_TARGET PkgConfig::SDL2_TTF)
    endif()

    find_library(X86EMU_LIBRARY x86emu REQUIRED)

    add_executable(sparky86
        src/sim/main.cpp
        src/sim/x86cpu.cpp
        src/sim/simgui.cpp
        src/sim/TextEditor.cpp
    )
    set_target_properties(sparky86 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_include_directories(sparky86 PRIVATE
        "${SIM_DIR}"
        "${SIMLIB_INCLUDE_DIR}"
        "${SIMLIB_IMGUI_INCLUDE_DIR}"
    )

    target_link_libraries(sparky86 PRIVATE
        "${SIMLIB_LIBRARY_DIR}/libsimlib.a"
        "${SIMLIB_LIBRARY_DIR}/libimgui.a"
        "${SIMLIB_LIBRARY_DIR}/liblua.a"
        "${SIMLIB_LIBRARY_DIR}/libbox2d.a"
        ${X86EMU_LIBRARY}
        ${SDL2_TTF_TARGET}
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        OpenGL::GL
        PNG::PNG
        ZLIB::ZLIB
        Vulkan::Vulkan
        SQLite::SQLite3
        OpenMP::OpenMP_CXX
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
endif()
