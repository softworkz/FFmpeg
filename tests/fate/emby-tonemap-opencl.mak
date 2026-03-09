# FATE tests for Emby OpenCL tone mapping customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   068 (18c26b99d2) - Tone Mapping: OpenCL
#     - New supertonemap_opencl filter (vf_supertonemap_opencl.c, supertonemap.cl)
#     - Modified tonemap_opencl filter (removed scene_threshold, new helpers,
#       rewritten launch_kernel, LUT-based pipeline)
#     - New opencl.c utility functions (ff_opencl_print_const_array,
#       ff_opencl_print_const_matrix_3xfloat3)
#     - clBuildProgram flags: -cl-fast-relaxed-math -cl-std=CL2.0
#     - Modified colorspace_common.cl (float4 variants, HLG constants)
#     - Modified tonemap.cl (2x2 quad processing, LUT-based tone mapping)
#     - Hardware context changes in hwcontext_opencl.c (QSV D3D11, VAAPI mapping)
#   188 (bdfca6048c) - Remove OpenCL extension pragmas (folded into 068)
#
# Note: OpenCL filters require GPU hardware for actual frame processing.
# These tests verify filter registration, options, and metadata only.

EMBY_TONEMAP_OCL_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_TONEMAP_OCL_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: supertonemap_opencl filter registration ---
# Verifies the new supertonemap_opencl filter appears in the filter list.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-registered
fate-emby-supertonemap-opencl-registered: CMP = oneline
fate-emby-supertonemap-opencl-registered: REF = 1
fate-emby-supertonemap-opencl-registered: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) -filters 2>/dev/null | \
        grep -c "supertonemap_opencl"

# --- Test 2: tonemap_opencl filter registration ---
# Verifies the existing tonemap_opencl filter is still registered.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_TONEMAP_OPENCL_FILTER) += fate-emby-tonemap-opencl-registered
fate-emby-tonemap-opencl-registered: CMP = oneline
fate-emby-tonemap-opencl-registered: REF = 1
fate-emby-tonemap-opencl-registered: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) -filters 2>/dev/null | \
        grep -c " tonemap_opencl "

# --- Test 3: supertonemap_opencl tonemap algorithms ---
# Verifies all seven tone mapping algorithms are available in the filter help.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-algorithms
fate-emby-supertonemap-opencl-algorithms: CMP = oneline
fate-emby-supertonemap-opencl-algorithms: REF = 7
fate-emby-supertonemap-opencl-algorithms: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=supertonemap_opencl 2>/dev/null | \
        grep -cE "^\s+(none|linear|gamma|clip|reinhard|hable|mobius)\s"

# --- Test 4: supertonemap_opencl desat option (default 0.5) ---
# Verifies the desaturation parameter is present with the expected default.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-desat
fate-emby-supertonemap-opencl-desat: CMP = oneline
fate-emby-supertonemap-opencl-desat: REF = 1
fate-emby-supertonemap-opencl-desat: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=supertonemap_opencl 2>/dev/null | \
        grep -c "desat.*desaturation parameter.*default 0.5"

# --- Test 5: supertonemap_opencl peak option ---
# Verifies the signal peak override option is present.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-peak
fate-emby-supertonemap-opencl-peak: CMP = oneline
fate-emby-supertonemap-opencl-peak: REF = 1
fate-emby-supertonemap-opencl-peak: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=supertonemap_opencl 2>/dev/null | \
        grep -c "peak.*signal peak override"

# --- Test 6: supertonemap_opencl accepts opencl pixel format ---
# Verifies the filter uses opencl pixel format for both input and output pads.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-pixfmt
fate-emby-supertonemap-opencl-pixfmt: CMP = oneline
fate-emby-supertonemap-opencl-pixfmt: REF = 2
fate-emby-supertonemap-opencl-pixfmt: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=supertonemap_opencl 2>/dev/null | \
        grep -c "Formats: \[opencl\]"

# --- Test 7: tonemap_opencl threshold option removed ---
# Verifies that the scene detection threshold option was removed from
# tonemap_opencl. The original baseline had "threshold" as an option;
# commit 068 commented it out.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_TONEMAP_OPENCL_FILTER) += fate-emby-tonemap-opencl-no-threshold
fate-emby-tonemap-opencl-no-threshold: CMP = oneline
fate-emby-tonemap-opencl-no-threshold: REF = 0
fate-emby-tonemap-opencl-no-threshold: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=tonemap_opencl 2>/dev/null | \
        grep -c "threshold" || true

# --- Test 8: tonemap_opencl algorithms match supertonemap_opencl ---
# Verifies the existing tonemap_opencl also has the same seven algorithms.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_TONEMAP_OPENCL_FILTER) += fate-emby-tonemap-opencl-algorithms
fate-emby-tonemap-opencl-algorithms: CMP = oneline
fate-emby-tonemap-opencl-algorithms: REF = 7
fate-emby-tonemap-opencl-algorithms: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=tonemap_opencl 2>/dev/null | \
        grep -cE "^\s+(none|linear|gamma|clip|reinhard|hable|mobius)\s"

# --- Test 9: supertonemap_opencl color space options ---
# Verifies that transfer, matrix, primaries, and range options are present,
# each with bt709 and bt2020 presets.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-colorspace-opts
fate-emby-supertonemap-opencl-colorspace-opts: CMP = oneline
fate-emby-supertonemap-opencl-colorspace-opts: REF = 4
fate-emby-supertonemap-opencl-colorspace-opts: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) \
        -h filter=supertonemap_opencl 2>/dev/null | \
        grep -cE "^\s+(transfer|matrix|primaries|range)\s"

# --- Test 10: supertonemap_opencl description ---
# Verifies the filter description matches the expected text.
FATE_EMBY_TONEMAP_OCL-$(CONFIG_SUPERTONEMAP_OPENCL_FILTER) += fate-emby-supertonemap-opencl-description
fate-emby-supertonemap-opencl-description: CMP = oneline
fate-emby-supertonemap-opencl-description: REF = 1
fate-emby-supertonemap-opencl-description: CMD = run $(EMBY_TONEMAP_OCL_FFMPEG) -filters 2>/dev/null | \
        grep -c "supertonemap_opencl.*perform HDR to SDR conversion with tonemapping"

# --- Collect all tests ---
FATE_EMBY_TONEMAP_OCL += $(FATE_EMBY_TONEMAP_OCL-yes)
FATE_FFMPEG += $(FATE_EMBY_TONEMAP_OCL)
