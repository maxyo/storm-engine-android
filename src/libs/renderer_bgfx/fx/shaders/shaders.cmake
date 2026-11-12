# Every shader variant of the fx package. Add a variant only together with the
# code path that selects it.
set(fx_sc "${CMAKE_CURRENT_LIST_DIR}")
set(fx_shaders)
storm_fx_add_shader(fx_shaders vs_fx_screen "${fx_sc}/vs_fx_screen.sc")
storm_fx_add_shader(fx_shaders fs_fx_copy "${fx_sc}/fs_fx_copy.sc")
storm_fx_add_shader(fx_shaders fs_fx_wobble "${fx_sc}/fs_fx_wobble.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_prefilter "${fx_sc}/fs_fx_bloom_prefilter.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_down "${fx_sc}/fs_fx_bloom_down.sc")
storm_fx_add_shader(fx_shaders fs_fx_bloom_up "${fx_sc}/fs_fx_bloom_up.sc")
storm_fx_add_shader(fx_shaders fs_fx_shafts "${fx_sc}/fs_fx_shafts.sc")
# Composite variants: every combination of bloom (b), tone curve (t) and ambient occlusion (a).
foreach(variant "" b t bt a ab at abt)
    set(defines)
    if(variant MATCHES "b")
        list(APPEND defines FX_BLOOM)
    endif()
    if(variant MATCHES "t")
        list(APPEND defines FX_TONEMAP)
    endif()
    if(variant MATCHES "a")
        list(APPEND defines FX_SSAO)
    endif()
    if(variant)
        storm_fx_add_shader(fx_shaders fs_fx_composite_${variant} "${fx_sc}/fs_fx_composite.sc" DEFINES ${defines})
    else()
        storm_fx_add_shader(fx_shaders fs_fx_composite "${fx_sc}/fs_fx_composite.sc")
    endif()
endforeach()
storm_fx_add_shader(fx_shaders fs_fx_ssao "${fx_sc}/fs_fx_ssao.sc")
# SMAA 1x (include/fx_smaa.sh): a vertex shader per pass for the reference's offsets.
storm_fx_add_shader(fx_shaders vs_fx_smaa_edges "${fx_sc}/vs_fx_smaa.sc" DEFINES FX_SMAA_EDGES)
storm_fx_add_shader(fx_shaders vs_fx_smaa_weights "${fx_sc}/vs_fx_smaa.sc" DEFINES FX_SMAA_WEIGHTS)
storm_fx_add_shader(fx_shaders vs_fx_smaa_blend "${fx_sc}/vs_fx_smaa_blend.sc")
foreach(pass edges weights blend)
    storm_fx_add_shader(fx_shaders fs_fx_smaa_${pass} "${fx_sc}/fs_fx_smaa_${pass}.sc")
endforeach()
# FSR 1 (include/fx_fsr.sh): EASU upscale, RCAS sharpening.
storm_fx_add_shader(fx_shaders fs_fx_easu "${fx_sc}/fs_fx_easu.sc")
storm_fx_add_shader(fx_shaders fs_fx_rcas "${fx_sc}/fs_fx_rcas.sc")


storm_fx_add_shader(fx_shaders fs_fx_sea_depth "${fx_sc}/fs_fx_sea_depth.sc")

storm_fx_add_shader(fx_shaders vs_fx_sprite "${fx_sc}/vs_fx_sprite.sc")
storm_fx_add_shader(fx_shaders vs_fx_sprite_world "${fx_sc}/vs_fx_sprite.sc" DEFINES FX_SPRITE_WORLD)
storm_fx_add_shader(fx_shaders fs_fx_sprite "${fx_sc}/fs_fx_sprite.sc")
storm_fx_add_shader(fx_shaders vs_fx_shadow_caster "${fx_sc}/vs_fx_shadow_caster.sc")
storm_fx_add_shader(fx_shaders fs_fx_shadow_caster "${fx_sc}/fs_fx_shadow_caster.sc")

storm_fx_build_shaders(TARGET storm_fx_shaders NAMESPACE storm_fx_shaders VARYING "${fx_sc}/varying.def.sc"
    INCLUDES "${fx_sc}/include" "${storm_smaa_SOURCE_DIR}" "${storm_fidelityfx_fsr_SOURCE_DIR}/ffx-fsr" SHADERS ${fx_shaders})
