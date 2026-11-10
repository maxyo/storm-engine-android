# RmlUi calls find_package(Freetype). The pinned FreeType source is already part of
# this build tree, so satisfy the lookup with that target instead of a system copy.
if(TARGET Freetype::Freetype)
    set(Freetype_FOUND TRUE)
    set(FREETYPE_FOUND TRUE)
endif()
