# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(altair_esp32)

# Keep enough file-descriptor space when increasing LWIP sockets
# FD_SETSIZE = sockets + non-socket FDs
idf_build_set_property(COMPILE_DEFINITIONS "FD_SETSIZE=96" APPEND)

# --------------------------------------------------------------------
# Workaround for ESP-IDF v6.0.1 picolibc.specs duplicate-flag bug.
#
# components/esp_libc/project_include.cmake adds an absolute-path
# "-specs=<build>/specs/picolibc.specs" via idf_toolchain_add_flags(),
# but the dedup helper (_add_flags_to_files) compares with STREQUAL,
# so a stale bare "-specs=picolibc.specs" line written by an earlier
# IDF reconfigure is left behind. GCC then loads the same specs file
# twice and aborts with:
#   "attempt to rename spec 'link' to already defined spec 'picolibc_link'"
#
# Strip the bare line from build/toolchain/{cflags,cxxflags,asmflags}
# after configure (all three response files inherit the duplicate).
# --------------------------------------------------------------------
foreach(_altair_tcfile cflags cxxflags asmflags)
    set(_altair_path "${CMAKE_BINARY_DIR}/toolchain/${_altair_tcfile}")
    if(EXISTS "${_altair_path}")
        file(STRINGS "${_altair_path}" _altair_lines)
        # Only strip the bare line if an absolute-path version is also
        # present (the actual v6.0.1 duplicate). On IDF v6.0 the bare
        # line is the ONLY specs flag and removing it would break
        # picolibc include resolution → newlib headers get used.
        set(_altair_has_abs FALSE)
        foreach(_line IN LISTS _altair_lines)
            if("${_line}" MATCHES "^-specs=.+/picolibc\\.specs$")
                set(_altair_has_abs TRUE)
            endif()
        endforeach()
        if(_altair_has_abs)
            set(_altair_new "")
            set(_altair_changed FALSE)
            foreach(_line IN LISTS _altair_lines)
                if("${_line}" STREQUAL "-specs=picolibc.specs")
                    set(_altair_changed TRUE)
                else()
                    list(APPEND _altair_new "${_line}")
                endif()
            endforeach()
            if(_altair_changed)
                message(STATUS "Pruning duplicate bare -specs=picolibc.specs from ${_altair_path}")
                file(WRITE "${_altair_path}" "")
                foreach(_line IN LISTS _altair_new)
                    file(APPEND "${_altair_path}" "${_line}\n")
                endforeach()
            endif()
        endif()
    endif()
endforeach()
