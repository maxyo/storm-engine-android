# cmake -DROOT=<repo> -DWORK=<dir> -DCASE=<apply|nothing> -P view.cmake
include("${ROOT}/cmake/StormBuildCache.cmake")
set(pristine "${WORK}/pristine")
set(view "${WORK}/_deps/lib-patched-0")
storm_patched_view("${pristine}" "${view}" . "${WORK}/fix.patch")
