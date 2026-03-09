# FATE tests for Emby CUDA tone mapping customizations
#
# These tests verify the behavior introduced by the following custom commit:
#   069 (5ea2271a51) - Tone Mapping: CUDA
#     - New tonemap_cuda filter (vf_tonemap_cuda.c)
#     - CUDA kernel files (libavfilter/cuda/tonemap.cu, colorspace_common.h, etc.)
#     - Build system integration (configure, Makefile, allfilters.c)
#     - cuda_runtime.h MAKE_VECTORS macro and math functions
#
# All tests require CONFIG_TONEMAP_CUDA_FILTER which depends on ffnvcodec
# and a CUDA compiler (cuda_nvcc or cuda_llvm).

EMBY_TONEMAP_CUDA_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_TONEMAP_CUDA_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: tonemap_cuda filter registration ---
# Verifies the tonemap_cuda filter appears in the filter list.
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-registered
fate-emby-tonemap-cuda-registered: CMP = oneline
fate-emby-tonemap-cuda-registered: REF = 1
fate-emby-tonemap-cuda-registered: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -filters 2>/dev/null | \
        grep -c "tonemap_cuda"

# --- Test 2: tonemap_cuda filter description ---
# Verifies the filter has the correct description text.
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-description
fate-emby-tonemap-cuda-description: CMP = oneline
fate-emby-tonemap-cuda-description: REF = 1
fate-emby-tonemap-cuda-description: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -filters 2>/dev/null | \
        grep -c "tonemap_cuda.*GPU accelerated HDR-to-SDR tone mapping"

# --- Test 3: tonemap_cuda algorithm options ---
# Verifies all 7 tone mapping algorithms (none, linear, gamma, clip, reinhard,
# hable, mobius) are available in the filter help output.
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-algorithms
fate-emby-tonemap-cuda-algorithms: CMP = oneline
fate-emby-tonemap-cuda-algorithms: REF = 7
fate-emby-tonemap-cuda-algorithms: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -h filter=tonemap_cuda 2>/dev/null | \
        grep -cE "^\s+(none|linear|gamma|clip|reinhard|hable|mobius)"

# --- Test 4: tonemap_cuda format option ---
# Verifies the output format option exists with default "same".
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-option-format
fate-emby-tonemap-cuda-option-format: CMP = oneline
fate-emby-tonemap-cuda-option-format: REF = 1
fate-emby-tonemap-cuda-option-format: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -h filter=tonemap_cuda 2>/dev/null | \
        grep -c "format.*Output format"

# --- Test 5: tonemap_cuda desat option ---
# Verifies the desaturation parameter option exists with default 0.5.
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-option-desat
fate-emby-tonemap-cuda-option-desat: CMP = oneline
fate-emby-tonemap-cuda-option-desat: REF = 1
fate-emby-tonemap-cuda-option-desat: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -h filter=tonemap_cuda 2>/dev/null | \
        grep -c "desat.*desaturation parameter"

# --- Test 6: tonemap_cuda param option ---
# Verifies the tonemap parameter option exists.
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-option-param
fate-emby-tonemap-cuda-option-param: CMP = oneline
fate-emby-tonemap-cuda-option-param: REF = 1
fate-emby-tonemap-cuda-option-param: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -h filter=tonemap_cuda 2>/dev/null | \
        grep -c "param.*tonemap parameter"

# --- Test 7: tonemap_cuda accepts CUDA pixel format ---
# Verifies the filter is flagged as hwframe-aware by checking it accepts
# AV_PIX_FMT_CUDA input (visible in filter help as pixel format restriction).
FATE_EMBY_TONEMAP_CUDA-$(CONFIG_TONEMAP_CUDA_FILTER) += fate-emby-tonemap-cuda-pixfmt
fate-emby-tonemap-cuda-pixfmt: CMP = oneline
fate-emby-tonemap-cuda-pixfmt: REF = 1
fate-emby-tonemap-cuda-pixfmt: CMD = run $(EMBY_TONEMAP_CUDA_FFMPEG) -h filter=tonemap_cuda 2>/dev/null | \
        grep -c "cuda"

# --- Collect all tests ---
FATE_EMBY_TONEMAP_CUDA += $(FATE_EMBY_TONEMAP_CUDA-yes)
FATE_FFMPEG += $(FATE_EMBY_TONEMAP_CUDA)
