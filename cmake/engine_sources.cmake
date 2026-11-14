# The pinned sources (tools/android/engine_sources.py) must be at their pins: a directory prepared for
# other pins (another worktree's, or the shared one before a bump) fails here, naming what differs,
# instead of compiling against the wrong versions. A pin change re-runs the configure step.
function(storm_check_engine_sources sources)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(script "${CMAKE_SOURCE_DIR}/tools/android/engine_sources.py")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${script}" --check "${sources}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${sources} does not hold the pinned engine sources (see above)")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
endfunction()
