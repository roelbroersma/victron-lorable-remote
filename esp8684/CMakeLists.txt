cmake_minimum_required(VERSION 3.16)

# The Windows esp-14.2.0 toolchain used by ESP-IDF 5.5.5 does not add its
# -fno-rtti C++ multilib headers to the normal search path. Keep the workaround
# project-local; never copy or patch files in the Espressif toolchain.
if(WIN32 AND DEFINED ENV{IDF_TOOLS_PATH})
    file(GLOB _esp_cxx_no_rtti
        "$ENV{IDF_TOOLS_PATH}/tools/riscv32-esp-elf/*/riscv32-esp-elf/riscv32-esp-elf/include/c++/*/riscv32-esp-elf/rv32imc_zicsr_zifencei/ilp32/no-rtti")
    if(_esp_cxx_no_rtti AND NOT "$ENV{EXTRA_CXXFLAGS}" MATCHES "no-rtti")
        list(GET _esp_cxx_no_rtti 0 _esp_cxx_no_rtti_dir)
        get_filename_component(_esp_cxx_ilp32_dir "${_esp_cxx_no_rtti_dir}" DIRECTORY)
        # project.cmake imports EXTRA_CXXFLAGS into IDF's cxxflags response
        # file, which also affects the C++ sources of framework components.
        set(ENV{EXTRA_CXXFLAGS}
            "$ENV{EXTRA_CXXFLAGS} -isystem${_esp_cxx_no_rtti_dir} -isystem${_esp_cxx_ilp32_dir}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "4.9.0-rc1")
project(lorable_esp8684)
