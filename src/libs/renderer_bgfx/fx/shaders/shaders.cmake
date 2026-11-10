# Every shader variant of the fx package. Add a variant only together with the
# code path that selects it.
set(fx_sc "${CMAKE_CURRENT_LIST_DIR}")
set(fx_shaders)
storm_fx_add_shader(fx_shaders vs_fx_screen "${fx_sc}/vs_fx_screen.sc")
storm_fx_add_shader(fx_shaders fs_fx_copy "${fx_sc}/fs_fx_copy.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_prefilter "${fx_sc}/fs_fx_bloom_prefilter.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_down "${fx_sc}/fs_fx_bloom_down.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_up "${fx_sc}/fs_fx_bloom_up.sc")
storm_fx_add_shader(fx_shaders fs_fx_shafts "${fx_sc}/fs_fx_shafts.sc")
storm_fx_add_shader(fx_shaders fs_fx_composite "${fx_sc}/fs_fx_composite.sc")
storm_fx_add_shader(fx_shaders fs_fx_composite_b "${fx_sc}/fs_fx_composite.sc" DEFINES FX_BLOOM)
storm_fx_add_shader(fx_shaders fs_fx_composite_t "${fx_sc}/fs_fx_composite.sc" DEFINES FX_TONEMAP)
storm_fx_add_shader(fx_shaders fs_fx_composite_bt "${fx_sc}/fs_fx_composite.sc" DEFINES FX_BLOOM FX_TONEMAP)
storm_fx_add_shader(fx_shaders fs_fx_fxaa "${fx_sc}/fs_fx_fxaa.sc")
storm_fx_add_shader(fx_shaders fs_fx_cas "${fx_sc}/fs_fx_cas.sc")

storm_fx_add_shader(fx_shaders vs_fx_sky "${fx_sc}/vs_fx_sky.sc")
storm_fx_add_shader(fx_shaders fs_fx_sky "${fx_sc}/fs_fx_sky.sc")
storm_fx_add_shader(fx_shaders fs_fx_sky_clouds "${fx_sc}/fs_fx_sky.sc" DEFINES FX_SKY_CLOUDS)

storm_fx_add_shader(fx_shaders vs_fx_water_mesh "${fx_sc}/vs_fx_water.sc" DEFINES FX_WATER_MESH)
storm_fx_add_shader(fx_shaders vs_fx_water_field "${fx_sc}/vs_fx_water.sc" DEFINES FX_WATER_FIELD)
storm_fx_add_shader(fx_shaders vs_fx_water_gerstner "${fx_sc}/vs_fx_water.sc" DEFINES FX_WATER_GERSTNER)
storm_fx_add_shader(fx_shaders fs_fx_water_low "${fx_sc}/fs_fx_water.sc")
storm_fx_add_shader(fx_shaders fs_fx_water_med "${fx_sc}/fs_fx_water.sc" DEFINES FX_WATER_FOAM FX_WATER_CLOUDS)
storm_fx_add_shader(fx_shaders fs_fx_water_med_cube "${fx_sc}/fs_fx_water.sc" DEFINES FX_WATER_FOAM FX_WATER_ENVCUBE)
storm_fx_add_shader(fx_shaders fs_fx_water_high "${fx_sc}/fs_fx_water.sc" DEFINES FX_WATER_FOAM FX_WATER_CLOUDS FX_WATER_DEPTH)
storm_fx_add_shader(fx_shaders fs_fx_water_high_cube "${fx_sc}/fs_fx_water.sc" DEFINES FX_WATER_FOAM FX_WATER_ENVCUBE FX_WATER_DEPTH)
storm_fx_add_shader(fx_shaders fs_fx_water_capture "${fx_sc}/fs_fx_water_capture.sc")

storm_fx_build_shaders(TARGET storm_fx_shaders NAMESPACE storm_fx_shaders VARYING "${fx_sc}/varying.def.sc"
    INCLUDES "${fx_sc}/include" SHADERS ${fx_shaders})
