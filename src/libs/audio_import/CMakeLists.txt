# Import of FMOD Studio banks (.bank/FSB5 Vorbis) and WAV into Ogg Vorbis for
# the open audio backend. No FMOD code or runtime is used. See README.md.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiph.cmake")
add_library(audio_import STATIC
    src/bank.cpp
    src/fsb5.cpp
    src/string_table.cpp
    src/vorbis_remux.cpp
    src/vorbis_setup.cpp
    src/wav_encode.cpp)
target_include_directories(audio_import PUBLIC include)
target_compile_features(audio_import PUBLIC cxx_std_20)
target_link_libraries(audio_import PUBLIC storm_vorbis storm_ogg)
if(NOT MSVC)
    target_compile_options(audio_import PRIVATE -Wall -Wextra)
endif()
