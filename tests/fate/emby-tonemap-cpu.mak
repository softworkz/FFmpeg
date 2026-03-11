# FATE tests for Emby CPU tone mapping customizations
#
# These tests verify the behavior introduced by the following custom commit:
#   067 (d69de4a50a) - Tone Mapping: CPU
#     - New supertonemap filter (vf_supertonemap.c, tonemap.h)
#     - Modified ff_determine_signal_peak() in colorspace.c
#     - Log message change in vf_tonemap.c

EMBY_TONEMAP_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_TONEMAP_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: supertonemap filter registration ---
# Verifies the supertonemap filter appears in the filter list.
# Only include when supertonemap filter is configured (e.g. Emby custom builds).
FATE_EMBY_TONEMAP-$(CONFIG_SUPERTONEMAP_FILTER) += fate-emby-supertonemap-registered
fate-emby-supertonemap-registered: CMP = oneline
fate-emby-supertonemap-registered: REF = 1
fate-emby-supertonemap-registered: CMD = run $(EMBY_TONEMAP_FFMPEG) -filters 2>/dev/null | \
        grep -cw "supertonemap"

# --- Test 2: supertonemap P010→NV12 with bt2390 (default algorithm) ---
# Converts a synthetic P010 HDR (BT.2020/PQ) source to NV12 SDR using the
# default bt2390 tone-mapping algorithm.
FATE_EMBY_TONEMAP-$(if $(CONFIG_SUPERTONEMAP_FILTER),$(call FILTERFRAMECRC, SUPERTONEMAP FORMAT SETPARAMS TESTSRC2)) += fate-emby-supertonemap-p010-bt2390
fate-emby-supertonemap-p010-bt2390: CMD = framecrc -lavfi testsrc2=r=2:d=1:s=64x64,format=p010,setparams=color_trc=smpte2084:color_primaries=bt2020:colorspace=bt2020nc,supertonemap=tonemap=bt2390 -pix_fmt nv12 -frames:v 2

# --- Test 3: supertonemap P010→NV12 with hable algorithm ---
# Tests the hable tone-mapping curve on the same P010 HDR source.
FATE_EMBY_TONEMAP-$(if $(CONFIG_SUPERTONEMAP_FILTER),$(call FILTERFRAMECRC, SUPERTONEMAP FORMAT SETPARAMS TESTSRC2)) += fate-emby-supertonemap-p010-hable
fate-emby-supertonemap-p010-hable: CMD = framecrc -lavfi testsrc2=r=2:d=1:s=64x64,format=p010,setparams=color_trc=smpte2084:color_primaries=bt2020:colorspace=bt2020nc,supertonemap=tonemap=hable -pix_fmt nv12 -frames:v 2

# --- Test 4: supertonemap P010→NV12 with mobius algorithm ---
# Tests the mobius tone-mapping curve on the same P010 HDR source.
FATE_EMBY_TONEMAP-$(if $(CONFIG_SUPERTONEMAP_FILTER),$(call FILTERFRAMECRC, SUPERTONEMAP FORMAT SETPARAMS TESTSRC2)) += fate-emby-supertonemap-p010-mobius
fate-emby-supertonemap-p010-mobius: CMD = framecrc -lavfi testsrc2=r=2:d=1:s=64x64,format=p010,setparams=color_trc=smpte2084:color_primaries=bt2020:colorspace=bt2020nc,supertonemap=tonemap=mobius -pix_fmt nv12 -frames:v 2

# --- Test 5: supertonemap output pixel format ---
# Verifies that supertonemap converts P010 input to NV12 output.
FATE_EMBY_TONEMAP-$(if $(CONFIG_SUPERTONEMAP_FILTER),$(call FILTERFRAMECRC, SUPERTONEMAP FORMAT SETPARAMS TESTSRC2)) += fate-emby-supertonemap-output-pixfmt
fate-emby-supertonemap-output-pixfmt: CMP = oneline
fate-emby-supertonemap-output-pixfmt: REF = nv12
fate-emby-supertonemap-output-pixfmt: CMD = run $(EMBY_TONEMAP_FFPROBE) -v quiet \
        -f lavfi -i "testsrc2=r=2:d=1:s=64x64,format=p010,setparams=color_trc=smpte2084:color_primaries=bt2020:colorspace=bt2020nc,supertonemap" \
        -show_entries stream=pix_fmt -of default=nw=1:nk=1

# --- Test 6: ff_determine_signal_peak default for PQ (SMPTE ST.2084) ---
# The modified function uses peak=10000 nits (100x REFERENCE_WHITE) for
# untagged PQ sources. Verify via debug log.
FATE_EMBY_TONEMAP-$(call FILTERFRAMECRC, TONEMAP FORMAT SETPARAMS TESTSRC2) += fate-emby-tonemap-peak-pq
fate-emby-tonemap-peak-pq: CMP = oneline
fate-emby-tonemap-peak-pq: REF = 1
fate-emby-tonemap-peak-pq: CMD = run $(EMBY_TONEMAP_FFMPEG) -v debug \
        -lavfi "testsrc2=r=2:d=1:s=64x64,format=gbrpf32,setparams=color_trc=smpte2084,tonemap=tonemap=linear" \
        -pix_fmt gbrpf32le -frames:v 1 -f null /dev/null 2>&1 | \
        grep -c "Setting default peak value: 10000"

# --- Test 7: ff_determine_signal_peak default for HLG ---
# Untagged (non-PQ) sources use 1000 nits (10x REFERENCE_WHITE).
FATE_EMBY_TONEMAP-$(call FILTERFRAMECRC, TONEMAP FORMAT TESTSRC2) += fate-emby-tonemap-peak-hlg
fate-emby-tonemap-peak-hlg: CMP = oneline
fate-emby-tonemap-peak-hlg: REF = 1
fate-emby-tonemap-peak-hlg: CMD = run $(EMBY_TONEMAP_FFMPEG) -v debug \
        -lavfi "testsrc2=r=2:d=1:s=64x64,format=gbrpf32,tonemap=tonemap=linear" \
        -pix_fmt gbrpf32le -frames:v 1 -f null /dev/null 2>&1 | \
        grep -c "Setting default peak value: 1000"

# --- Test 8: tonemap log message change ---
# Verifies the log message was changed from "Computed signal peak" to
# "Peak from side data" in vf_tonemap.c.
FATE_EMBY_TONEMAP-$(call FILTERFRAMECRC, TONEMAP FORMAT TESTSRC2) += fate-emby-tonemap-log-message
fate-emby-tonemap-log-message: CMP = oneline
fate-emby-tonemap-log-message: REF = 1
fate-emby-tonemap-log-message: CMD = run $(EMBY_TONEMAP_FFMPEG) -v debug \
        -lavfi "testsrc2=r=2:d=1:s=64x64,format=gbrpf32,tonemap=tonemap=linear" \
        -pix_fmt gbrpf32le -frames:v 1 -f null /dev/null 2>&1 | \
        grep -c "Peak from side data"

# --- Collect all tests ---
FATE_EMBY_TONEMAP += $(FATE_EMBY_TONEMAP-yes)
FATE_FFMPEG += $(FATE_EMBY_TONEMAP)
