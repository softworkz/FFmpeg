# FATE tests for Emby QSV SEI parsing chain
#
# These tests verify the behavior introduced by the following custom commits:
#   108 (4618e65d36) - avcodec/vpp_qsv: Copy side data from input to output frame
#   109 (17b037f387) - avcodec/mpeg12dec: make mpeg_decode_user_data() accessible
#   110 (7969787e33) - avcodec/hevcdec: make set_side_data() accessible
#   111 (e88fa50d48) - avcodec/h264dec: make h264_export_frame_props() accessible
#   112 (eb8f893dee) - avcodec/qsvdec: Implement SEI parsing for QSV decoders
#
# Commit 108 is obsolete (new baseline covers it) and is not tested here.
# Commit 112 (QSV SEI parsing) requires Intel QSV hardware and cannot be
# exercised in a software-only FATE run. Tests below verify commits 109-111
# which refactor internal functions to be accessible with parameterized
# signatures. The normal software decode path exercises these refactored
# functions, confirming the new signatures work correctly.

EMBY_QSV_SEI_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_QSV_SEI_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Generated test data paths ---
EMBY_QSV_SEI_H264_BASE  = tests/data/emby-qsv-sei-h264-base.h264
EMBY_QSV_SEI_H264_CC    = tests/data/emby-qsv-sei-h264-cc.h264
EMBY_QSV_SEI_MPEG2_BASE = tests/data/emby-qsv-sei-mpeg2-base.m2v
EMBY_QSV_SEI_MPEG2_CC   = tests/data/emby-qsv-sei-mpeg2-cc.m2v
EMBY_QSV_SEI_HEVC_HDR   = tests/data/emby-qsv-sei-hevc-hdr.hevc

# --- Test data generation ---

# H.264 base stream (no captions)
tests/data/emby-qsv-sei-h264-base.h264: $(EMBY_QSV_SEI_FFMPEG) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i testsrc=duration=0.04:size=64x64:rate=25 \
        -c:v libx264 -preset ultrafast \
        -flags +bitexact -fflags +bitexact \
        -f h264 -y $(TARGET_PATH)/$@ 2>/dev/null

# H.264 with A/53 CC SEI injected
tests/data/emby-qsv-sei-h264-cc.h264: $(EMBY_QSV_SEI_H264_BASE) | tests/data
	$(M)python3 $(SRC_PATH)/tests/emby-h264-cc-inject.py \
        $(TARGET_PATH)/$(EMBY_QSV_SEI_H264_BASE) $(TARGET_PATH)/$@

# MPEG-2 base stream (no captions)
tests/data/emby-qsv-sei-mpeg2-base.m2v: $(EMBY_QSV_SEI_FFMPEG) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i testsrc=duration=0.04:size=64x64:rate=25 \
        -c:v mpeg2video \
        -flags +bitexact -fflags +bitexact \
        -f mpeg2video -y $(TARGET_PATH)/$@ 2>/dev/null

# MPEG-2 with A/53 CC user data injected
tests/data/emby-qsv-sei-mpeg2-cc.m2v: $(EMBY_QSV_SEI_MPEG2_BASE) | tests/data
	$(M)python3 $(SRC_PATH)/tests/emby-mpeg2-cc-inject.py \
        $(TARGET_PATH)/$(EMBY_QSV_SEI_MPEG2_BASE) $(TARGET_PATH)/$@

# HEVC with mastering display and content light level SEI
tests/data/emby-qsv-sei-hevc-hdr.hevc: $(EMBY_QSV_SEI_FFMPEG) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i testsrc=duration=0.04:size=64x64:rate=25 \
        -c:v libx265 -preset ultrafast \
        -x265-params "master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1):max-cll=1000,400" \
        -flags +bitexact -fflags +bitexact \
        -f hevc -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-qsv-sei-h264-base.h264 tests/data/emby-qsv-sei-h264-cc.h264 tests/data/emby-qsv-sei-mpeg2-base.m2v tests/data/emby-qsv-sei-mpeg2-cc.m2v tests/data/emby-qsv-sei-hevc-hdr.hevc: TAG = GEN

# --- Test 1: H.264 decoder exports A/53 CC frame side data (commit 111) ---
# Verifies that ff_h264_export_frame_props() with its refactored signature
# correctly exports A/53 closed captions as AV_FRAME_DATA_A53_CC frame side
# data. The injected H.264 stream contains an A/53 caption SEI NAL, and the
# software decoder must produce a frame with the A53 CC side data type.
FATE_EMBY_QSV_SEI-$(call ALLYES, LIBX264_ENCODER H264_DEMUXER H264_DECODER TESTSRC_FILTER) += fate-emby-qsv-sei-h264-a53cc
fate-emby-qsv-sei-h264-a53cc: $(EMBY_QSV_SEI_H264_CC)
fate-emby-qsv-sei-h264-a53cc: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries frame_side_data_list \
        -of default $(TARGET_PATH)/$(EMBY_QSV_SEI_H264_CC)

# --- Test 2: H.264 decoder — no false positive for CC (precondition) ---
# Verifies that without A/53 caption SEI, the A53 CC side data type does NOT
# appear. Only the x264 encoder's unregistered user data SEI should be present.
FATE_EMBY_QSV_SEI-$(call ALLYES, LIBX264_ENCODER H264_DEMUXER H264_DECODER TESTSRC_FILTER) += fate-emby-qsv-sei-h264-no-cc
fate-emby-qsv-sei-h264-no-cc: $(EMBY_QSV_SEI_H264_BASE)
fate-emby-qsv-sei-h264-no-cc: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries frame_side_data_list \
        -of default $(TARGET_PATH)/$(EMBY_QSV_SEI_H264_BASE)

# --- Test 3: MPEG-2 decoder exports A/53 CC frame side data (commit 109) ---
# Verifies that ff_mpeg_decode_user_data() with its refactored signature
# correctly parses A/53 closed caption user data, and the MPEG-2 decoder
# exports it as AV_FRAME_DATA_A53_CC frame side data. The injected MPEG-2
# stream contains user_data_start_code with GA94 A/53 caption data.
FATE_EMBY_QSV_SEI-$(call ALLYES, MPEG2VIDEO_ENCODER MPEG2VIDEO_DECODER MPEGVIDEO_DEMUXER TESTSRC_FILTER) += fate-emby-qsv-sei-mpeg2-a53cc
fate-emby-qsv-sei-mpeg2-a53cc: $(EMBY_QSV_SEI_MPEG2_CC)
fate-emby-qsv-sei-mpeg2-a53cc: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries frame_side_data_list \
        -of default $(TARGET_PATH)/$(EMBY_QSV_SEI_MPEG2_CC)

# --- Test 4: MPEG-2 decoder — no false positive for CC (precondition) ---
# Verifies that without A/53 caption user data, the A53 CC side data does NOT
# appear. Only the pan-scan side data from the MPEG-2 encoder should be present.
FATE_EMBY_QSV_SEI-$(call ALLYES, MPEG2VIDEO_ENCODER MPEG2VIDEO_DECODER MPEGVIDEO_DEMUXER TESTSRC_FILTER) += fate-emby-qsv-sei-mpeg2-no-cc
fate-emby-qsv-sei-mpeg2-no-cc: $(EMBY_QSV_SEI_MPEG2_BASE)
fate-emby-qsv-sei-mpeg2-no-cc: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries frame_side_data_list \
        -of default $(TARGET_PATH)/$(EMBY_QSV_SEI_MPEG2_BASE)

# --- Test 5: HEVC decoder exports mastering display metadata (commit 110) ---
# Verifies that ff_set_side_data() with its refactored signature correctly
# exports mastering display colour volume SEI as frame side data. The HEVC
# stream is encoded with x265 master-display parameters.
FATE_EMBY_QSV_SEI-$(call ALLYES, LIBX265_ENCODER HEVC_DEMUXER HEVC_DECODER TESTSRC_FILTER) += fate-emby-qsv-sei-hevc-mastering
fate-emby-qsv-sei-hevc-mastering: $(EMBY_QSV_SEI_HEVC_HDR)
fate-emby-qsv-sei-hevc-mastering: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries "frame_side_data=side_data_type,red_x,red_y,green_x,green_y,blue_x,blue_y,white_point_x,white_point_y,min_luminance,max_luminance" \
        -of default -select_streams v \
        $(TARGET_PATH)/$(EMBY_QSV_SEI_HEVC_HDR)

# --- Test 6: HEVC decoder exports content light level metadata (commit 110) ---
# Verifies that ff_set_side_data() correctly exports content light level info
# SEI as frame side data (max_content and max_average fields).
FATE_EMBY_QSV_SEI-$(call ALLYES, LIBX265_ENCODER HEVC_DEMUXER HEVC_DECODER TESTSRC_FILTER) += fate-emby-qsv-sei-hevc-cll
fate-emby-qsv-sei-hevc-cll: $(EMBY_QSV_SEI_HEVC_HDR)
fate-emby-qsv-sei-hevc-cll: CMD = run $(EMBY_QSV_SEI_FFPROBE) -bitexact \
        -show_entries "frame_side_data=side_data_type,max_content,max_average" \
        -of default -select_streams v \
        $(TARGET_PATH)/$(EMBY_QSV_SEI_HEVC_HDR)

# --- Collect all tests ---
FATE_EMBY_QSV_SEI += $(FATE_EMBY_QSV_SEI-yes)
FATE_FFPROBE += $(FATE_EMBY_QSV_SEI)
fate-emby-qsv-sei: $(FATE_EMBY_QSV_SEI)
