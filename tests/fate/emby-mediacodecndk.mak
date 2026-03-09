# FATE tests for Emby MediaCodecNDK work unit
#
# These tests verify the behavior introduced by the following custom commits:
#   029 (ca5f911c11) - Add MediaCodecNDK Codecs (infrastructure: send_packet
#                       callback in FFCodec, send_pkt field in AVCodecInternal)
#   101 (65727781f7) - Fix mediacodecndk issues with 4.4, 4.5, 5.0
#   132 (0c480309fd) - avcodec/mediacodecndk: adjust for recent changes
#   105 (53ff356008) - avformat/segment: fix segfault due to extradata double free
#   106 (e8e53f8a07) - extradata: fix extradata handling (improved propagation)
#
# The MediaCodecNDK codecs themselves require Android NDK and cannot be tested
# on a standard Linux host. These tests focus on the segment muxer extradata
# fixes (commits 105/106) and the decoder infrastructure safety (commit 029).

EMBY_MCNDK_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_MCNDK_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: Segment extradata deep copy — all segments valid (commits 105/106) ---
# Creates H.264 MPEGTS segments and verifies the last (non-first) segment is a
# valid MPEGTS container. Before commit 105, the segment muxer performed a
# shallow extradata pointer copy which could cause double-free crashes or
# corrupt output in later segments.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, SEGMENT_MUXER MPEGTS_MUXER MPEGTS_DEMUXER LIBX264_ENCODER H264_DECODER) += fate-emby-mediacodecndk-segment-valid
fate-emby-mediacodecndk-segment-valid: CMP = oneline
fate-emby-mediacodecndk-segment-valid: REF = mpegts
fate-emby-mediacodecndk-segment-valid: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=3:r=25" \
        -c:v libx264 -preset ultrafast -g 25 -flags +bitexact \
        -f segment -segment_time 1 -segment_format mpegts \
        -y $(TARGET_PATH)/tests/data/emby-mcndk-segval-%03d.ts 2>/dev/null && \
        run $(EMBY_MCNDK_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/tests/data/emby-mcndk-segval-002.ts

# --- Test 2: Segment extradata present in all segments (commits 105/106) ---
# Creates H.264 MPEGTS segments and counts how many have non-zero extradata.
# The deep copy fix ensures every segment receives proper extradata rather than
# a dangling pointer to freed memory. All 3 segments must have extradata_size > 0.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, SEGMENT_MUXER MPEGTS_MUXER MPEGTS_DEMUXER LIBX264_ENCODER) += fate-emby-mediacodecndk-segment-extradata
fate-emby-mediacodecndk-segment-extradata: CMP = oneline
fate-emby-mediacodecndk-segment-extradata: REF = 3
fate-emby-mediacodecndk-segment-extradata: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=3:r=25" \
        -c:v libx264 -preset ultrafast -g 25 -flags +bitexact \
        -f segment -segment_time 1 -segment_format mpegts \
        -y $(TARGET_PATH)/tests/data/emby-mcndk-segext-%03d.ts 2>/dev/null && \
        count=0; \
        for f in $(TARGET_PATH)/tests/data/emby-mcndk-segext-000.ts \
                 $(TARGET_PATH)/tests/data/emby-mcndk-segext-001.ts \
                 $(TARGET_PATH)/tests/data/emby-mcndk-segext-002.ts; do \
            sz=$$(run $(EMBY_MCNDK_FFPROBE) -v quiet \
                -show_entries stream=extradata_size -of csv=p=0 "$$f" | head -1); \
            if [ "$$sz" -gt 0 ] 2>/dev/null; then count=$$((count + 1)); fi; \
        done; echo $$count

# --- Test 3: Segment extradata — later segments decodable (commits 105/106) ---
# Encodes H.264 to MPEGTS segments and decodes the second segment (not the
# first) to verify its extradata is correct and allows full decode. If extradata
# were corrupted by a shallow copy, the decoder would fail. Counts decoded
# frames via framecrc output lines.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, SEGMENT_MUXER MPEGTS_MUXER MPEGTS_DEMUXER LIBX264_ENCODER H264_DECODER) += fate-emby-mediacodecndk-segment-decode
fate-emby-mediacodecndk-segment-decode: CMP = oneline
fate-emby-mediacodecndk-segment-decode: REF = 25
fate-emby-mediacodecndk-segment-decode: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=3:r=25" \
        -c:v libx264 -preset ultrafast -g 25 -flags +bitexact \
        -f segment -segment_time 1 -segment_format mpegts \
        -y $(TARGET_PATH)/tests/data/emby-mcndk-segdec-%03d.ts 2>/dev/null && \
        run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -i $(TARGET_PATH)/tests/data/emby-mcndk-segdec-001.ts \
        -f framecrc - 2>/dev/null | grep -c "^0,"

# --- Test 4: Segment stress — many segment init/close cycles (commits 105/106) ---
# Creates many short segments from H.264 content to stress-test the repeated
# segment_mux_init/close cycle. Each cycle allocates and frees extradata; the
# shallow copy bug would manifest as crashes under this workload. Verifies the
# last segment is still a valid container.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, SEGMENT_MUXER MPEGTS_MUXER MPEGTS_DEMUXER LIBX264_ENCODER) += fate-emby-mediacodecndk-segment-stress
fate-emby-mediacodecndk-segment-stress: CMP = oneline
fate-emby-mediacodecndk-segment-stress: REF = mpegts
fate-emby-mediacodecndk-segment-stress: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=5:r=25" \
        -c:v libx264 -preset ultrafast -g 10 -flags +bitexact \
        -f segment -segment_time 0.5 -segment_format mpegts -break_non_keyframes 1 \
        -y $(TARGET_PATH)/tests/data/emby-mcndk-segstress-%03d.ts 2>/dev/null && \
        run $(EMBY_MCNDK_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/tests/data/emby-mcndk-segstress-009.ts

# --- Test 5: Decoder flush safety with send_pkt infrastructure (commit 029) ---
# Commit 029 added av_packet_unref(&avci->send_pkt) to the flush path in
# avcodec_flush_buffers(). This runs for ALL decoders, not just mediacodecndk.
# Verify that seeking (which triggers flush) followed by decoding works
# correctly and produces valid output. Counts decoded frames via framecrc.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, MPEGTS_MUXER MPEGTS_DEMUXER LIBX264_ENCODER H264_DECODER) += fate-emby-mediacodecndk-flush-safe
fate-emby-mediacodecndk-flush-safe: CMP = oneline
fate-emby-mediacodecndk-flush-safe: REF = 5
fate-emby-mediacodecndk-flush-safe: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=2:r=25" \
        -c:v libx264 -preset ultrafast -g 25 -flags +bitexact \
        -f mpegts -y $(TARGET_PATH)/tests/data/emby-mcndk-flush.ts 2>/dev/null && \
        run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -ss 0.5 -i $(TARGET_PATH)/tests/data/emby-mcndk-flush.ts \
        -frames:v 5 -f framecrc - 2>/dev/null | grep -c "^0,"

# --- Test 6: Segment muxer SegmentComplete with H.264 extradata (commits 105/106) ---
# Verifies that segmenting H.264 content produces the expected number of
# SegmentComplete log lines, confirming that the segment muxer completes all
# segments without crashing during extradata handling.
FATE_EMBY_MEDIACODECNDK-$(call ALLYES, SEGMENT_MUXER MPEGTS_MUXER LIBX264_ENCODER) += fate-emby-mediacodecndk-segment-complete
fate-emby-mediacodecndk-segment-complete: CMP = oneline
fate-emby-mediacodecndk-segment-complete: REF = 3
fate-emby-mediacodecndk-segment-complete: CMD = run $(EMBY_MCNDK_FFMPEG) -nostdin \
        -f lavfi -i "color=c=blue:s=16x16:d=3:r=25" \
        -c:v libx264 -preset ultrafast -g 25 -flags +bitexact \
        -f segment -segment_time 1 -segment_format mpegts \
        -y $(TARGET_PATH)/tests/data/emby-mcndk-segcomp-%03d.ts 2>&1 | \
        grep -c "SegmentComplete="

# --- Collect all tests ---
FATE_EMBY_MEDIACODECNDK += $(FATE_EMBY_MEDIACODECNDK-yes)
FATE_FFMPEG += $(FATE_EMBY_MEDIACODECNDK)
