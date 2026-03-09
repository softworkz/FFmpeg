# FATE tests for Emby HLS encoder logging customizations
#
# These tests verify the behavior introduced by:
#   098 (bb087af5b8) - avformat/hlsenc: Add 'SegmentComplete' logging
#   100 (bd163e1203) - avformat/hlsenc: Log InitFileComplete message
#
# Commit 098 modifies the SegmentComplete log in hls_write_packet() to use
# integer PTS values (StartPts/EndPts) instead of float positions, and removes
# the fMP4 segment type exclusion so the log fires for all segment types.
#
# Commit 100 adds an InitFileComplete log in hls_init_file_resend(), emitted
# each time an fMP4 init file is resent after a segment boundary.

EMBY_HLS_FFMPEG = ffmpeg$(PROGSSUF)$(EXESUF)

# --- Test 1: HLS SegmentComplete uses PTS format (commit 098) ---
# Verifies that the SegmentComplete log line contains StartPts= fields,
# confirming the PTS-based format introduced by commit 098.
FATE_EMBY_HLSENC_LOGGING-$(call ALLYES, HLS_MUXER MPEGTS_MUXER AAC_ENCODER PCM_S16LE_DECODER) += fate-emby-hls-segment-complete-pts
fate-emby-hls-segment-complete-pts: $(AREF)
fate-emby-hls-segment-complete-pts: CMP = oneline
fate-emby-hls-segment-complete-pts: REF = 3
fate-emby-hls-segment-complete-pts: CMD = run $(EMBY_HLS_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f hls -hls_time 1 -hls_segment_type mpegts -map 0 -flags +bitexact \
        -acodec aac -b:a 64k \
        -y $(TARGET_PATH)/tests/data/emby-hls-enc-pts.m3u8 2>&1 | \
        grep -c "SegmentComplete=.*StartPts="

# --- Test 2: HLS SegmentComplete for fMP4 segments (commit 098) ---
# Verifies that the SegmentComplete log fires for fMP4 segment type.
# Prior to commit 098, fMP4 segments were excluded from SegmentComplete logging.
FATE_EMBY_HLSENC_LOGGING-$(call ALLYES, HLS_MUXER MOV_MUXER AAC_ENCODER PCM_S16LE_DECODER) += fate-emby-hls-fmp4-segment-complete
fate-emby-hls-fmp4-segment-complete: $(AREF)
fate-emby-hls-fmp4-segment-complete: CMP = oneline
fate-emby-hls-fmp4-segment-complete: REF = 3
fate-emby-hls-fmp4-segment-complete: CMD = run $(EMBY_HLS_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f hls -hls_time 1 -hls_segment_type fmp4 -map 0 -flags +bitexact \
        -acodec aac -b:a 64k \
        -y $(TARGET_PATH)/tests/data/emby-hls-fmp4-seg.m3u8 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 3: InitFileComplete log (commit 100) ---
# Verifies that the InitFileComplete log is emitted when hls_fmp4_init_resend
# is enabled, confirming the logging added by commit 100.
FATE_EMBY_HLSENC_LOGGING-$(call ALLYES, HLS_MUXER MOV_MUXER AAC_ENCODER PCM_S16LE_DECODER) += fate-emby-hls-init-file-complete
fate-emby-hls-init-file-complete: $(AREF)
fate-emby-hls-init-file-complete: CMP = oneline
fate-emby-hls-init-file-complete: REF = 3
fate-emby-hls-init-file-complete: CMD = run $(EMBY_HLS_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f hls -hls_time 1 -hls_segment_type fmp4 \
        -hls_fmp4_init_resend 1 -map 0 -flags +bitexact \
        -acodec aac -b:a 64k \
        -y $(TARGET_PATH)/tests/data/emby-hls-initfile.m3u8 2>&1 | \
        grep -c "InitFileComplete"

# --- Collect all tests ---
FATE_EMBY_HLSENC_LOGGING += $(FATE_EMBY_HLSENC_LOGGING-yes)
FATE_FFMPEG += $(FATE_EMBY_HLSENC_LOGGING)
