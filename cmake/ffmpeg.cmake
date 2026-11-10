# FFmpeg (LGPL-2.1-or-later, shared libraries) for in-game video (src/libs/video).
# tools/ffmpeg/build.py configures and builds the pinned source once per source revision,
# flag set and compiler, at configure time, into a cache shared by all build directories that
# use the same prepared sources. Flags and licence notes: tools/ffmpeg/README.md.
#   STORM_FFMPEG_SOURCE  pinned checkout (tools/android/engine_sources.py), default <sources>/ffmpeg
#   STORM_FFMPEG_CACHE   build cache, default <sources>/../ffmpeg-cache
function(storm_ffmpeg sources target)
    set(STORM_FFMPEG_SOURCE "${sources}/ffmpeg" CACHE PATH "Pinned FFmpeg source tree")
    get_filename_component(default_cache "${sources}/../ffmpeg-cache" ABSOLUTE)
    set(STORM_FFMPEG_CACHE "${default_cache}" CACHE PATH "FFmpeg build cache")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(command "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/ffmpeg/build.py" --source "${STORM_FFMPEG_SOURCE}"
        --cache "${STORM_FFMPEG_CACHE}" --target ${target})
    if(target MATCHES "^android-")
        list(APPEND command --ndk "${STORM_ANDROID_NDK}")
    endif()
    execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "FFmpeg build failed (tools/ffmpeg/build.py)")
    endif()
    message(STATUS "FFmpeg: ${prefix}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/ffmpeg/build.py")
    add_library(storm_ffmpeg INTERFACE)
    target_include_directories(storm_ffmpeg SYSTEM INTERFACE "${prefix}/include")
    # Packaged next to libengine.so (the application copies runtime/ffmpeg/*.so into the APK).
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
    foreach(name avformat avcodec swscale swresample avutil)
        add_library(storm_ffmpeg_${name} SHARED IMPORTED GLOBAL)
        set_target_properties(storm_ffmpeg_${name} PROPERTIES IMPORTED_LOCATION "${prefix}/lib/lib${name}.so")
        target_link_libraries(storm_ffmpeg INTERFACE storm_ffmpeg_${name})
        if(target MATCHES "^android-")
            file(COPY "${prefix}/lib/lib${name}.so" DESTINATION "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
        endif()
    endforeach()
    if(target MATCHES "^android-")
        file(COPY "${prefix}/configure.txt" DESTINATION "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
    endif()
endfunction()
