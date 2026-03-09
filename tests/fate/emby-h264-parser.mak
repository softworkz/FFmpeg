# FATE tests for Emby H264 parser customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   031 (64fee12498) - h264 parser: Set sample aspect ratio
#   065 (d729a93c22) - H264 parser: Set closed captions flag
#
# Commit 064 (17615d9833) - Fix sample aspect ratio when scaling in hardware
# is not tested here as it requires a hardware-accelerated encoding pipeline.

EMBY_H264_SAR_FILE  = tests/data/emby-h264-sar.h264
EMBY_H264_BASE_FILE = tests/data/emby-h264-base.h264
EMBY_H264_CC_FILE   = tests/data/emby-h264-cc.h264

EMBY_H264P_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_H264P_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test data generation ---

tests/data/emby-h264-sar.h264: $(EMBY_H264P_FFMPEG) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i testsrc=duration=0.04:size=160x120:rate=25 \
        -c:v libx264 -preset ultrafast -vf setsar=40/33 \
        -flags +bitexact -fflags +bitexact \
        -f h264 -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-h264-base.h264: $(EMBY_H264P_FFMPEG) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i testsrc=duration=0.04:size=160x120:rate=25 \
        -c:v libx264 -preset ultrafast \
        -flags +bitexact -fflags +bitexact \
        -f h264 -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-h264-cc.h264: $(EMBY_H264_BASE_FILE) | tests/data
	$(M)python3 $(SRC_PATH)/tests/emby-h264-cc-inject.py \
        $(TARGET_PATH)/$(EMBY_H264_BASE_FILE) $(TARGET_PATH)/$@

tests/data/emby-h264-sar.h264 tests/data/emby-h264-base.h264 tests/data/emby-h264-cc.h264: TAG = GEN

# --- Test 1: Parser propagates SAR from SPS (commit 031) ---
# Verifies that the H264 parser reads SAR from the SPS and sets it on the
# codec context via ff_set_sar(). ffprobe reports this as sample_aspect_ratio.
FATE_EMBY_H264_PARSER-$(call ALLYES, LIBX264_ENCODER H264_DEMUXER H264_PARSER TESTSRC_FILTER SETSAR_FILTER) += fate-emby-h264-parser-sar
fate-emby-h264-parser-sar: $(EMBY_H264_SAR_FILE)
fate-emby-h264-parser-sar: CMD = run $(EMBY_H264P_FFPROBE) -bitexact \
        -show_entries stream=sample_aspect_ratio,display_aspect_ratio \
        -of default $(TARGET_PATH)/$(EMBY_H264_SAR_FILE)

# --- Test 2: Parser detects A/53 closed captions SEI (commit 065) ---
# Verifies that the H264 parser detects A/53 caption data in SEI and sets
# FF_CODEC_PROPERTY_CLOSED_CAPTIONS on the codec context.
FATE_EMBY_H264_PARSER-$(call ALLYES, LIBX264_ENCODER H264_DEMUXER H264_PARSER TESTSRC_FILTER) += fate-emby-h264-parser-cc
fate-emby-h264-parser-cc: $(EMBY_H264_CC_FILE)
fate-emby-h264-parser-cc: CMD = run $(EMBY_H264P_FFPROBE) -bitexact \
        -show_entries stream=closed_captions \
        -of default $(TARGET_PATH)/$(EMBY_H264_CC_FILE)

# --- Test 3: No false positive for closed captions (precondition) ---
# Verifies that without A/53 caption SEI, closed_captions remains 0.
FATE_EMBY_H264_PARSER-$(call ALLYES, LIBX264_ENCODER H264_DEMUXER H264_PARSER TESTSRC_FILTER) += fate-emby-h264-parser-cc-absent
fate-emby-h264-parser-cc-absent: $(EMBY_H264_BASE_FILE)
fate-emby-h264-parser-cc-absent: CMD = run $(EMBY_H264P_FFPROBE) -bitexact \
        -show_entries stream=closed_captions \
        -of default $(TARGET_PATH)/$(EMBY_H264_BASE_FILE)

# --- Collect all tests ---
FATE_EMBY_H264_PARSER += $(FATE_EMBY_H264_PARSER-yes)
FATE_FFPROBE += $(FATE_EMBY_H264_PARSER)
fate-emby-h264-parser: $(FATE_EMBY_H264_PARSER)
