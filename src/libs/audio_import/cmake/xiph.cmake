# Pinned libogg/libvorbis sources for audio_import (BSD-3-Clause; notices in
# third_party/licenses/). Their own CMake files predate CMake 3.5 and are not
# used: the targets below compile the upstream sources directly.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_LIBOGG=<checkout> and
#          -DFETCHCONTENT_SOURCE_DIR_STORM_LIBVORBIS=<checkout> at these commits.
include(FetchContent)
FetchContent_Declare(storm_libogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG be05b13e98b048f0b5a0f5fa8ce514d56db5f822 # v1.3.6
    SOURCE_SUBDIR storm-not-a-cmake-project)
FetchContent_Declare(storm_libvorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG 0657aee69dec8508a0011f47f3b69d7538e9d262 # v1.3.7
    SOURCE_SUBDIR storm-not-a-cmake-project)
FetchContent_MakeAvailable(storm_libogg storm_libvorbis)

if(NOT TARGET storm_ogg)
    set(storm_ogg_generated "${CMAKE_CURRENT_BINARY_DIR}/storm_ogg_include")
    set(INCLUDE_INTTYPES_H 0)
    set(INCLUDE_STDINT_H 1)
    set(INCLUDE_SYS_TYPES_H 0)
    set(SIZE16 int16_t)
    set(USIZE16 uint16_t)
    set(SIZE32 int32_t)
    set(USIZE32 uint32_t)
    set(SIZE64 int64_t)
    set(USIZE64 uint64_t)
    configure_file("${storm_libogg_SOURCE_DIR}/include/ogg/config_types.h.in" "${storm_ogg_generated}/ogg/config_types.h" @ONLY)
    add_library(storm_ogg STATIC "${storm_libogg_SOURCE_DIR}/src/bitwise.c" "${storm_libogg_SOURCE_DIR}/src/framing.c")
    target_include_directories(storm_ogg SYSTEM PUBLIC "${storm_ogg_generated}" "${storm_libogg_SOURCE_DIR}/include")
    target_compile_options(storm_ogg PRIVATE -w)

    set(storm_vorbis_lib "${storm_libvorbis_SOURCE_DIR}/lib")
    # libvorbis decoder/encoder core plus vorbisenc; vorbisfile is not needed.
    set(storm_vorbis_sources mdct smallft block envelope window lsp lpc analysis synthesis psy info
        floor1 floor0 res0 mapping0 registry codebook sharedbook lookup bitrate vorbisenc)
    list(TRANSFORM storm_vorbis_sources PREPEND "${storm_vorbis_lib}/")
    list(TRANSFORM storm_vorbis_sources APPEND ".c")
    add_library(storm_vorbis STATIC ${storm_vorbis_sources})
    target_include_directories(storm_vorbis SYSTEM PUBLIC "${storm_libvorbis_SOURCE_DIR}/include" PRIVATE "${storm_vorbis_lib}")
    target_link_libraries(storm_vorbis PUBLIC storm_ogg)
    if(NOT MSVC)
        target_link_libraries(storm_vorbis PUBLIC m)
    endif()
    target_compile_options(storm_vorbis PRIVATE -w)
endif()
