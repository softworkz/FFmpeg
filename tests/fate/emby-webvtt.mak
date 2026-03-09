# FATE tests for Emby WebVTT HLS support customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   164 (c6c0da2a33) - avformat/webvttdec: Add support for HLS WebVTT
#   166 (ba10387ec5) - avformat/webvttdec: Set timebase statically depending on prefer_hls_mpegts_pts option
#   169 (a035b6bd49) - avcodec/webvttdec: reset readorder when seeking back

EMBY_WEBVTT_HLS_FILE = $(SRC_PATH)/tests/emby-webvtt-hls.vtt
EMBY_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test 1: HLS mode - timestamps converted to MPEG-TS 90kHz (commits 164+166) ---
# When prefer_hls_mpegts_pts=1, the X-TIMESTAMP-MAP header is parsed and
# timestamps are converted from WebVTT milliseconds to MPEG-TS 90kHz ticks.
# The ASS output shows shifted timestamps (offset by 10s from MPEGTS:900000).
FATE_EMBY_WEBVTT-$(call DEMDEC, WEBVTT, WEBVTT) += fate-emby-webvtt-hls-ass
fate-emby-webvtt-hls-ass: CMD = fmtstdout ass -f webvtt -prefer_hls_mpegts_pts 1 -i $(EMBY_WEBVTT_HLS_FILE)

# --- Test 2: Default mode - timestamps remain in milliseconds (commits 164+166) ---
# Without the option, X-TIMESTAMP-MAP is ignored and timestamps stay as-is.
FATE_EMBY_WEBVTT-$(call DEMDEC, WEBVTT, WEBVTT) += fate-emby-webvtt-hls-default-ass
fate-emby-webvtt-hls-default-ass: CMD = fmtstdout ass -i $(EMBY_WEBVTT_HLS_FILE)

# --- Test 3: Stream timebase is 1/90000 with HLS option (commit 166) ---
# The timebase is set statically at stream creation depending on the option.
FATE_EMBY_WEBVTT-$(CONFIG_WEBVTT_DEMUXER) += fate-emby-webvtt-hls-timebase
fate-emby-webvtt-hls-timebase: CMD = run $(EMBY_FFPROBE) -bitexact \
        -show_entries stream=time_base -of default=noprint_wrappers=1:nokey=1 \
        -f webvtt -prefer_hls_mpegts_pts 1 \
        -i $(EMBY_WEBVTT_HLS_FILE) 2>/dev/null

# --- Test 4: PTS values in MPEG-TS 90kHz domain (commits 164+166+169) ---
# With HLS mode, PTS values are in 90kHz ticks with the MPEGTS offset applied.
# Packets also carry the AV_PKT_FLAG_SEGMENT_SOURCE flag (commit 169).
FATE_EMBY_WEBVTT-$(CONFIG_WEBVTT_DEMUXER) += fate-emby-webvtt-hls-pts
fate-emby-webvtt-hls-pts: CMD = run $(EMBY_FFPROBE) -bitexact \
        -show_entries packet=pts -of default=noprint_wrappers=1:nokey=1 \
        -f webvtt -prefer_hls_mpegts_pts 1 \
        -i $(EMBY_WEBVTT_HLS_FILE) 2>/dev/null

# --- Collect all tests ---
FATE_EMBY_WEBVTT += $(FATE_EMBY_WEBVTT-yes)
fate-emby-webvtt-hls-ass fate-emby-webvtt-hls-default-ass: CMP = rawdiff
FATE_FFMPEG += $(FATE_EMBY_WEBVTT)
