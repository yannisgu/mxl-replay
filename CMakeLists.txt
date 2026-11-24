cmake_minimum_required(VERSION 3.24)
project(mxl-replay VERSION 1.8.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(REPLAY_BUILD_TESTS "Build unit tests" ON)
option(REPLAY_WITH_NMOS "Link Sony nmos-cpp" OFF)
set(NMOS_CPP_DIR "" CACHE PATH "Path to sony/nmos-cpp Development/")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)

add_library(replay-core STATIC
    src/util/sha1.cpp
    src/util/uuid.cpp
    src/util/logging.cpp
    src/util/json.cpp
    src/media/timebase.cpp
    src/media/format.cpp
    src/media/v210.cpp
    src/media/jpeg.cpp
    src/media/picture.cpp
    src/media/vmx.cpp
    src/media/audio.cpp
    src/media/anc.cpp
    src/media/scale.cpp
    src/media/convert.cpp
    src/media/mosaic.cpp
    src/flow/dis.cpp
    src/flow/cuda_flow.cpp
    src/flow/ofa.cpp
    src/record/hfr.cpp
    src/record/ring.cpp
    src/playout/scheduler.cpp
    src/playout/decodeahead.cpp
    src/playout/flowahead.cpp
    src/playout/readahead.cpp
    src/playout/shotbox.cpp
    src/playout/transition.cpp
    src/tally/tsl.cpp
    src/library/catalog.cpp
    src/config/config.cpp
    src/domain/scan.cpp
    src/nmos/ids.cpp
    src/ops/metrics.cpp
    src/ops/httpserver.cpp
    src/ops/api.cpp
    src/ops/preview.cpp
    src/ops/publisher.cpp
    src/ops/child.cpp
    src/app/engine.cpp
    src/mxl/io.cpp
    src/nmos/node.cpp
)
target_include_directories(replay-core PUBLIC src third_party)
target_link_libraries(replay-core PUBLIC Threads::Threads PkgConfig::JPEG PkgConfig::SQLITE PkgConfig::AV ${CMAKE_DL_LIBS})
target_compile_options(replay-core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# VMX (Open Media Transport's codec, third_party/libvmx) as the stored picture codec, REPLAY_CODEC=vmx.
# Like upstream's Linux build, it is compiled for AVX2 + BMI; vmxAvailable() checks the CPU at run time.
option(REPLAY_WITH_VMX "Build the VMX picture codec (x86-64)" ON)
if(REPLAY_WITH_VMX AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_library(vmx STATIC
        third_party/libvmx/src/vmxcodec.cpp
        third_party/libvmx/src/vmxcodec_x86.cpp
        third_party/libvmx/src/vmxcodec_avx2.cpp
    )
    target_include_directories(vmx PUBLIC third_party/libvmx/src)
    set_target_properties(vmx PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
    target_compile_options(vmx PRIVATE -O3 -mavx2 -mbmi -mlzcnt -w -include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvmx/vmx_gcc.h)
    target_link_libraries(vmx PUBLIC Threads::Threads)
    target_link_libraries(replay-core PUBLIC vmx)
    target_compile_definitions(replay-core PUBLIC REPLAY_WITH_VMX=1)
    # libvmx alone, without the replay's conversions (docs/benchmarks).
    add_executable(vmx-probe tests/bench/vmx_probe.cpp)
    target_link_libraries(vmx-probe PRIVATE vmx)
endif()

# One binary: nvcc + nvJPEG when the toolkit is installed, CPU fallback when it is not.
# sm_75 (Turing), sm_86 (A4000), sm_89 (L4). The runtime image does not ship a
# driver; the NVIDIA container toolkit injects libcuda at start.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(CMAKE_CUDA_ARCHITECTURES 75 86 89)
    enable_language(CUDA)
    target_sources(replay-core PRIVATE src/flow/dis_cuda.cu)
    set_target_properties(replay-core PROPERTIES CUDA_ARCHITECTURES "75;86;89" CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
    target_link_libraries(replay-core PUBLIC CUDA::cudart CUDA::nvjpeg)
    target_compile_definitions(replay-core PUBLIC REPLAY_WITH_CUDA=1 REPLAY_WITH_NVJPEG=1)
endif()

if(REPLAY_BUILD_TESTS)
    enable_testing()
    add_executable(unit-tests
        tests/unit/test_main.cpp
        tests/unit/test_replay.cpp
        tests/unit/test_preview.cpp
        tests/unit/test_flowahead.cpp
        tests/unit/test_tally.cpp
        tests/unit/test_transitions.cpp
        tests/unit/test_interlaced.cpp
        tests/unit/test_upload_rate.cpp
        tests/unit/test_config_file.cpp
        tests/unit/test_upload_stream.cpp
        tests/unit/test_readahead.cpp
        tests/unit/test_decodeahead.cpp
        tests/unit/test_vmx.cpp
        tests/unit/test_blend.cpp
    )
    target_link_libraries(unit-tests PRIVATE replay-core)
    target_compile_options(unit-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME unit-tests COMMAND unit-tests)
endif()

# Encode and decode speed, size and PSNR of the picture codecs on real pictures (docs/benchmarks).
add_executable(mxl-replay-codec-bench tests/bench/codec_bench.cpp)
target_link_libraries(mxl-replay-codec-bench PRIVATE replay-core)
# The codecs' encode with many cameras encoding at once (docs/benchmarks).
add_executable(mxl-replay-encode-scale tests/bench/encode_scale.cpp)
target_link_libraries(mxl-replay-encode-scale PRIVATE replay-core)
if(TARGET vmx)
    # A paced VMX encode against a flat-out one (docs/benchmarks).
    add_executable(vmx-pace tests/bench/vmx_pace.cpp)
    target_link_libraries(vmx-pace PRIVATE replay-core)
endif()
# Where a CPU slow-motion blend grain spends its time, before and after the v210 blend (docs/benchmarks).
add_executable(mxl-replay-blend-bench tests/bench/blend_bench.cpp)
target_link_libraries(mxl-replay-blend-bench PRIVATE replay-core)

find_package(mxl CONFIG QUIET)
set(REPLAY_UI_SOURCE "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/web/dist/index.html)
    set(WEBUI_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/ops/webui_generated.hpp)
    add_custom_command(
        OUTPUT ${WEBUI_GENERATED}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/web/dist/index.html -DOUTPUT=${WEBUI_GENERATED} -DVAR=indexHtml
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
        DEPENDS web/dist/index.html cmake/EmbedFile.cmake
        COMMENT "Embedding web UI"
    )
    set(REPLAY_UI_SOURCE ${WEBUI_GENERATED})
endif()

add_executable(mxl-replay src/main.cpp ${REPLAY_UI_SOURCE})
target_link_libraries(mxl-replay PRIVATE replay-core)
target_compile_options(mxl-replay PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(REPLAY_UI_SOURCE)
    target_compile_definitions(mxl-replay PRIVATE REPLAY_HAS_UI=1)
    target_include_directories(mxl-replay PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()
if(mxl_FOUND)
    target_compile_definitions(replay-core PUBLIC REPLAY_WITH_MXL=1)
    target_link_libraries(replay-core PUBLIC mxl::mxl)
endif()
if(REPLAY_WITH_NMOS)
    if(NOT NMOS_CPP_DIR)
        message(FATAL_ERROR "REPLAY_WITH_NMOS=ON requires -DNMOS_CPP_DIR")
    endif()
    set(NMOS_CPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NMOS_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NMOS_CPP_USE_SUPPLIED_JSON_SCHEMA_VALIDATOR ON CACHE BOOL "" FORCE)
    set(NMOS_CPP_USE_SUPPLIED_JWT_CPP ON CACHE BOOL "" FORCE)
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(${NMOS_CPP_DIR} ${CMAKE_BINARY_DIR}/nmos-cpp EXCLUDE_FROM_ALL)
    set(CMAKE_CXX_STANDARD 20)
    target_compile_definitions(replay-core PUBLIC REPLAY_WITH_NMOS=1)
    target_link_libraries(replay-core PUBLIC nmos-cpp::nmos-cpp)
    set_source_files_properties(src/nmos/node.cpp PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        COMPILE_OPTIONS "-w")
endif()

add_executable(mxl-replay-harness tests/harness/harness.cpp)
target_link_libraries(mxl-replay-harness PRIVATE replay-core)
target_compile_features(mxl-replay-harness PRIVATE cxx_std_20)
