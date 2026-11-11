cmake_minimum_required(VERSION 3.16)
project(SDLPoP)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=c99 -D_GNU_SOURCE=1")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=implicit-function-declaration")

# have CMake output binaries to the directory that contains the source files
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SDLPoP_SOURCE_DIR}/..")

# SDLPoP (this fork) requires SDL3 3.4 or newer (PNG loading is built into SDL3;
# SDL_image is not needed). Point CMake at it with -DCMAKE_PREFIX_PATH=/path/to/SDL3
# if it is not installed system-wide. The SDL2 API the game was written for is
# mapped onto SDL3 by sdl2_compat.h.
# For MS-DOS, see ../msdos/Makefile.
find_package(SDL3 REQUIRED CONFIG)

if (WIN32 AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    # Hide the console window with MinGW, except in debug builds.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mwindows")
endif()

set(SOURCE_FILES
        main.c
        common.h
        config.h
        data.c
        data.h
        proto.h
        types.h
        seg000.c
        seg001.c
        seg002.c
        seg003.c
        seg004.c
        seg005.c
        seg006.c
        seg007.c
        seg008.c
        seg009.c
        seqtbl.c
        options.c
        midi.c opl3.c opl3.h
        replay.c
        lighting.c
        screenshot.c
        menu.c
        stb_vorbis.c
        sdl2_compat.c sdl2_compat.h
        dos_video.c
        icon.rc
        )

if (NOT(APPLE AND CREATE_BUNDLE))
    add_executable(prince ${SOURCE_FILES})
else()

    # macOS-specific:
    # By default, a normal binary executable will be created.
    # If you want an Application Bundle instead, pass CREATE_BUNDLE to CMake like so: -DCREATE_BUNDLE=1

    # NOTE: The SDL3 framework is not copied into the bundle; it must be installed.

    set (SDLPoP_VERSION "1.24 RC")
    set (MACOSX_BUNDLE_INFO_STRING          ${PROJECT_NAME})
    set (MACOSX_BUNDLE_ICON_FILE            "icon.icns")
    set_source_files_properties(${MACOSX_BUNDLE_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set (MACOSX_BUNDLE_GUI_IDENTIFIER       "org.princed.SDLPoP")
    set (MACOSX_BUNDLE_LONG_VERSION_STRING  ${SDLPoP_VERSION})
    set (MACOSX_BUNDLE_BUNDLE_NAME          "SDLPoP")
    set (MACOSX_BUNDLE_SHORT_VERSION_STRING ${SDLPoP_VERSION})
    set (MACOSX_BUNDLE_BUNDLE_VERSION       ${SDLPoP_VERSION})
    set (MACOSX_BUNDLE_COPYRIGHT            "GNU General Public Licence, v3")
    add_executable(prince MACOSX_BUNDLE ${SOURCE_FILES} ${MACOSX_BUNDLE_ICON_FILE})
    add_custom_command(TARGET prince POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/../data ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/prince.app/Contents/MacOS/data)
    add_custom_command(TARGET prince POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/../doc ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/prince.app/Contents/MacOS/doc)
    add_custom_command(TARGET prince POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/../SDLPoP.ini ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/prince.app/Contents/MacOS/SDLPoP.ini)
endif()

if(PSP)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
	target_link_libraries(prince PRIVATE SDL3::SDL3)
	create_pbp_file(
		TARGET prince
		ICON_PATH ${CMAKE_SOURCE_DIR}/iconPSP.png
		BACKGROUND_PATH NULL
		PREVIEW_PATH NULL
		TITLE prince
	)
else()
	target_link_libraries(prince PRIVATE SDL3::SDL3)
	if (NOT WIN32 AND NOT APPLE)
		target_link_libraries(prince PRIVATE m)
	endif()
endif()
