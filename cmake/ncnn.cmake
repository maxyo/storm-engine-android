# ncnn (Tencent, BSD-3-Clause; its bundled glslang under its own licences: third_party/licenses/ncnn-*, glslang-*): the
# relief networks on the GPU through Vulkan compute (src/libs/relief/README.md). The release's full source (with
# glslang, which compiles ncnn's compute shaders on the device) by its pin; built with the in-house Vulkan loader
# (simplevk: libvulkan is opened at run time, no headers or loader needed to build), without OpenMP (its CPU path is
# not used: onnxruntime runs the CPU path), with the layers the PBRify networks and the convolutions inside them use.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/StormBuildCache.cmake")

storm_fetch_declare(storm_ncnn
    URL https://github.com/Tencent/ncnn/releases/download/20261020/ncnn-20261020-full-source.zip
    URL_HASH SHA256=699d9acc67489eee0993a551ca1ebc247bbf0f4e33babe1dec810e6a9a52b988
    STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_ncnn)

set(NCNN_VULKAN ON CACHE BOOL "" FORCE)
set(NCNN_SIMPLEVK ON CACHE BOOL "" FORCE)
set(NCNN_SYSTEM_GLSLANG OFF CACHE BOOL "" FORCE)
set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
set(NCNN_THREADS ON CACHE BOOL "" FORCE)
set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
set(NCNN_C_API OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL_ROTATE OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL_AFFINE OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL_DRAWING OFF CACHE BOOL "" FORCE)
set(NCNN_INT8 OFF CACHE BOOL "" FORCE)
set(NCNN_BF16 OFF CACHE BOOL "" FORCE)
# NCNN_RUNTIME_CPU stays on: its CPU kernels are built for each instruction set and picked at run time (built for the
# host's highest one alone, they stop on an older CPU with an illegal instruction).
set(NCNN_RUNTIME_CPU ON CACHE BOOL "" FORCE)
set(NCNN_DISABLE_EXCEPTION OFF CACHE BOOL "" FORCE)
set(NCNN_DISABLE_RTTI OFF CACHE BOOL "" FORCE)
# The layers: the networks' own (pnnx's ncnn graphs: Input, Convolution, Split, Swish, Sigmoid, BinaryOp, Concat,
# PixelShuffle) and the ones ncnn's convolutions create inside (Padding, Packing, Cast, Crop, Flatten, Reshape, Gemm,
# InnerProduct, ...). Every other layer is left out of the build.
set(storm_ncnn_layers Input Convolution ConvolutionDepthWise Split Swish Sigmoid BinaryOp UnaryOp Concat PixelShuffle Padding
    Packing Cast Crop Flatten Reshape Gemm InnerProduct Interp ReLU Eltwise Noop Slice Permute MemoryData Softmax Quantize)
file(STRINGS "${storm_ncnn_SOURCE_DIR}/src/CMakeLists.txt" storm_ncnn_all REGEX "^ncnn_add_layer\\(")
foreach(line ${storm_ncnn_all})
    string(REGEX MATCH "ncnn_add_layer\\(([A-Za-z0-9_]+)" _ "${line}")
    if(CMAKE_MATCH_1 IN_LIST storm_ncnn_layers)
        set(WITH_LAYER_${CMAKE_MATCH_1} ON CACHE BOOL "" FORCE)
    else()
        set(WITH_LAYER_${CMAKE_MATCH_1} OFF CACHE BOOL "" FORCE)
    endif()
endforeach()
add_subdirectory("${storm_ncnn_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/storm_ncnn-build" EXCLUDE_FROM_ALL)
set_target_properties(ncnn PROPERTIES POSITION_INDEPENDENT_CODE ON)
