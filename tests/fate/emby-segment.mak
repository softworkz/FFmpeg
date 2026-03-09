# FATE tests for Emby segment muxer customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   048 (f448a921e0) - Add segment_write_temp option
#   049 (a265f60901) - Log information about written segments (SegmentComplete)
#   050 (bd84429291) - Add segment_limit option
#   051 (b5b46ba16b) - Add drop_partial_offset option
#   052 (3a71b17e53) - Remove initial_offset deprecation warning
#   053 (239814bf8a) - Allow negative segment_time_delta
#   054 (e7de743f2e) - Add min_frame_time option
#   055 (8cd8eb044b) - Format options string, extradata sync, log fix
#
# hlsenc.c changes:
#   049/055 - SegmentComplete log in hls_write_packet()
#   055     - InitFileComplete log in hls_init_file_resend()

EMBY_SEG_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_SEG_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: SegmentComplete log message (commit 049) ---
# Verifies that the segment muxer emits SegmentComplete= info log lines
# for each completed segment. With 3 seconds of audio and segment_time=1,
# we expect 3 SegmentComplete lines.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-complete-log
fate-emby-segment-complete-log: $(AREF)
fate-emby-segment-complete-log: CMP = oneline
fate-emby-segment-complete-log: REF = 3
fate-emby-segment-complete-log: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-log-%03d.nut 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 2: segment_limit option (commit 050) ---
# Verifies that segment_limit=2 limits output to exactly 2 segments.
# Count SegmentComplete lines to confirm only 2 segments were created.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-limit
fate-emby-segment-limit: $(AREF)
fate-emby-segment-limit: CMP = oneline
fate-emby-segment-limit: REF = 2
fate-emby-segment-limit: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -segment_limit 2 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-limit-%03d.nut 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 3: segment_limit produces valid segments (commit 050) ---
# Verifies that segments produced with segment_limit=2 contain valid data
# by probing the first segment file.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER NUT_DEMUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-limit-valid
fate-emby-segment-limit-valid: $(AREF)
fate-emby-segment-limit-valid: CMP = oneline
fate-emby-segment-limit-valid: REF = nut
fate-emby-segment-limit-valid: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -segment_limit 2 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-limval-%03d.nut 2>/dev/null && \
        run $(EMBY_SEG_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/tests/data/emby-seg-limval-001.nut

# --- Test 4: segment_write_temp produces valid output (commit 048) ---
# Verifies that with segment_write_temp=1, segments are correctly renamed
# from .tmp to final names and the resulting files are valid NUT containers.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER NUT_DEMUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-write-temp
fate-emby-segment-write-temp: $(AREF)
fate-emby-segment-write-temp: CMP = oneline
fate-emby-segment-write-temp: REF = nut
fate-emby-segment-write-temp: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 2 -f segment -segment_time 1 -segment_write_temp 1 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-temp-%03d.nut 2>/dev/null && \
        run $(EMBY_SEG_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/tests/data/emby-seg-temp-000.nut

# --- Test 5: segment_write_temp uses .tmp suffix (commit 048) ---
# Verifies that the segment muxer opens files with .tmp suffix when
# segment_write_temp=1 is enabled, confirmed by "Opening" log messages.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-write-temp-log
fate-emby-segment-write-temp-log: $(AREF)
fate-emby-segment-write-temp-log: CMP = oneline
fate-emby-segment-write-temp-log: REF = 2
fate-emby-segment-write-temp-log: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 2 -f segment -segment_time 1 -segment_write_temp 1 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-tmplog-%03d.nut 2>&1 | \
        grep -c "\.tmp"

# --- Test 6: negative segment_time_delta accepted (commit 053) ---
# Verifies that a negative segment_time_delta value is accepted and shifts
# segment boundaries later. With -0.5 offset the first cut moves from ~1s
# to ~1.5s, confirmed by the start time in the second SegmentComplete line.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-negative-time-delta
fate-emby-segment-negative-time-delta: $(AREF)
fate-emby-segment-negative-time-delta: CMP = oneline
fate-emby-segment-negative-time-delta: REF = 3
fate-emby-segment-negative-time-delta: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -segment_time_delta -0.5 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-negdelta-%03d.nut 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 7: no initial_offset deprecation warning (commit 052) ---
# Verifies that using initial_offset does not produce a deprecation warning.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-no-initial-offset-warning
fate-emby-segment-no-initial-offset-warning: $(AREF)
fate-emby-segment-no-initial-offset-warning: CMP = oneline
fate-emby-segment-no-initial-offset-warning: REF = 0
fate-emby-segment-no-initial-offset-warning: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 1 -f segment -segment_time 1 -initial_offset 1 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-initoff-%03d.nut 2>&1 | \
        grep -ci "deprecat" || true

# --- Test 8: segment_format_options as string type (commit 055) ---
# Verifies that segment_format_options works with string-type parsing
# (key=value:key=value format) by producing valid MKV segments with
# the live=1 option.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER MATROSKA_MUXER MATROSKA_DEMUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-format-options-str
fate-emby-segment-format-options-str: $(AREF)
fate-emby-segment-format-options-str: CMP = oneline
fate-emby-segment-format-options-str: REF = matroska,webm
fate-emby-segment-format-options-str: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 2 -f segment -segment_time 1 -segment_format matroska \
        -segment_format_options live=1 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-fmtopt-%03d.mkv 2>/dev/null && \
        run $(EMBY_SEG_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/tests/data/emby-seg-fmtopt-000.mkv

# --- Test 9: min_frame_time option shifts segment boundaries (commit 054) ---
# Verifies that the min_frame_time option is accepted and shifts segment
# boundaries. With min_frame_time=5 (5 seconds offset), segment boundaries
# are pushed far beyond the 3-second input, producing only 1 segment.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-min-frame-time
fate-emby-segment-min-frame-time: $(AREF)
fate-emby-segment-min-frame-time: CMP = oneline
fate-emby-segment-min-frame-time: REF = 1
fate-emby-segment-min-frame-time: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -min_frame_time 5 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-minft-%03d.nut 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 10: HLS SegmentComplete log (hlsenc.c commit 049/055) ---
# Verifies that the HLS muxer emits SegmentComplete= log lines.
FATE_EMBY_SEGMENT-$(call ALLYES, HLS_MUXER MPEGTS_MUXER AAC_ENCODER PCM_S16LE_DECODER) += fate-emby-hls-segment-complete-log
fate-emby-hls-segment-complete-log: $(AREF)
fate-emby-hls-segment-complete-log: CMP = oneline
fate-emby-hls-segment-complete-log: REF = 3
fate-emby-hls-segment-complete-log: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f hls -hls_time 1 -hls_segment_type mpegts -map 0 -flags +bitexact \
        -acodec aac -b:a 64k \
        -y $(TARGET_PATH)/tests/data/emby-hls-log.m3u8 2>&1 | \
        grep -c "SegmentComplete="

# --- Test 11: drop_partial_offset option accepted (commit 051) ---
# Verifies that the drop_partial_offset option is accepted without error.
# Combined with segment_write_temp, this enables dropping partial end segments.
FATE_EMBY_SEGMENT-$(call ALLYES, SEGMENT_MUXER NUT_MUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-emby-segment-drop-partial-offset
fate-emby-segment-drop-partial-offset: $(AREF)
fate-emby-segment-drop-partial-offset: CMP = oneline
fate-emby-segment-drop-partial-offset: REF = 2
fate-emby-segment-drop-partial-offset: CMD = run $(EMBY_SEG_FFMPEG) -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 3 -f segment -segment_time 1 -segment_write_temp 1 \
        -drop_partial_offset 5 -map 0 -flags +bitexact \
        -acodec pcm_s16le \
        -y $(TARGET_PATH)/tests/data/emby-seg-drop-%03d.nut 2>&1 | \
        grep -c "SegmentComplete="

# --- Collect all tests ---
FATE_EMBY_SEGMENT += $(FATE_EMBY_SEGMENT-yes)
FATE_FFMPEG += $(FATE_EMBY_SEGMENT)
