# FATE tests for AC-4 decoder integration
#
# These tests verify the behavior introduced by the following custom commits:
#   080 (7a6cc99b8f) - AC-4 audio decoder (ac4dec.c, ac4dec_data.h)
#   081 (faad758e5b) - Raw AC-4 demuxer (ac4dec.c in libavformat)
#   082 (6eb9097f01) - MPEG-TS AC-4 descriptor detection
#   083-087, 133, 185 - Bug fixes and adaptations (in decoder)
#   135 (592481aa40) - AC-4 frame duration in get_audio_frame_duration()
#   136 (77bfe97553) - MPEG-TS PMT registration descriptor for AC-4

EMBY_AC4_FFMPEG  = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_AC4_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# Synthetic AC-4 probe file: 3 minimal frames with 0xAC40 sync words
EMBY_AC4_PROBE_FILE = tests/data/emby-ac4-probe.ac4

tests/data/emby-ac4-probe.ac4: | tests/data
	$(M)printf '\254\100\000\010\000\000\000\000\000\000\000\000\254\100\000\010\000\000\000\000\000\000\000\000\254\100\000\010\000\000\000\000\000\000\000\000' > $(TARGET_PATH)/$@

tests/data/emby-ac4-probe.ac4: TAG = GEN

# --- Test 1: AC-4 decoder registration (commits 080, 083-087, 133, 185) ---
# Verifies that the AC-4 decoder is present in the codec list.
FATE_EMBY_AC4 += fate-emby-ac4-decoder-registered
fate-emby-ac4-decoder-registered: CMP = oneline
fate-emby-ac4-decoder-registered: REF = 1
fate-emby-ac4-decoder-registered: CMD = run $(EMBY_AC4_FFPROBE) -codecs 2>/dev/null | \
        grep -c "D\.A\.L\. ac4"

# --- Test 2: AC-4 demuxer registration (commit 081) ---
# Verifies that the raw AC-4 demuxer is present in the format list.
FATE_EMBY_AC4 += fate-emby-ac4-demuxer-registered
fate-emby-ac4-demuxer-registered: CMP = oneline
fate-emby-ac4-demuxer-registered: REF = 1
fate-emby-ac4-demuxer-registered: CMD = run $(EMBY_AC4_FFPROBE) -formats 2>/dev/null | \
        grep -c "ac4.*raw.AC-4"

# --- Test 3: AC-4 demuxer probe (commit 081) ---
# Verifies that the AC-4 demuxer correctly identifies a synthetic AC-4 file
# containing valid sync words (0xAC40).
FATE_EMBY_AC4-$(CONFIG_AC4_DEMUXER) += fate-emby-ac4-probe
fate-emby-ac4-probe: $(EMBY_AC4_PROBE_FILE)
fate-emby-ac4-probe: CMP = oneline
fate-emby-ac4-probe: REF = ac4
fate-emby-ac4-probe: CMD = run $(EMBY_AC4_FFPROBE) -v quiet \
        -show_entries format=format_name -of default=nw=1:nk=1 \
        $(TARGET_PATH)/$(EMBY_AC4_PROBE_FILE)

# --- Test 4: AC-4 codec descriptor (commit 080) ---
# Verifies that the AC-4 codec has the correct descriptor properties.
FATE_EMBY_AC4-$(CONFIG_AC4_DEMUXER) += fate-emby-ac4-codec-descriptor
fate-emby-ac4-codec-descriptor: $(EMBY_AC4_PROBE_FILE)
fate-emby-ac4-codec-descriptor: CMD = run $(EMBY_AC4_FFPROBE) -v quiet \
        -show_entries stream=codec_name,codec_long_name,codec_type \
        -of default=nw=1 \
        $(TARGET_PATH)/$(EMBY_AC4_PROBE_FILE)

# --- Test 5: AC-4 frame duration C API test (commit 135) ---
# Verifies that av_get_audio_frame_duration2() returns 1536 for AC-4,
# decoder is registered with correct name/type, and has expected capabilities.
FATE_EMBY_AC4_API-$(CONFIG_AC4_DECODER) += fate-api-ac4
fate-api-ac4: $(APITESTSDIR)/api-ac4-test$(EXESUF)
fate-api-ac4: CMD = run $(APITESTSDIR)/api-ac4-test$(EXESUF)
fate-api-ac4: CMP = null

# --- Test 6: MPEG-TS AC-4 PMT descriptor and demux (commits 082, 136) ---
# Uses C API to mux an AC-4 stream into MPEG-TS, verifies the PMT contains the
# 'AC-4' registration descriptor, and demuxes to verify codec identification.
FATE_EMBY_AC4_API-$(call ALLYES, MPEGTS_MUXER MPEGTS_DEMUXER) += fate-api-ac4-mpegts
fate-api-ac4-mpegts: $(APITESTSDIR)/api-ac4-mpegts-test$(EXESUF)
fate-api-ac4-mpegts: CMD = run $(APITESTSDIR)/api-ac4-mpegts-test$(EXESUF)
fate-api-ac4-mpegts: CMP = null

# --- Collect all tests ---
FATE_EMBY_AC4 += $(FATE_EMBY_AC4-yes)
FATE_EMBY_AC4 += $(FATE_EMBY_AC4_API-yes)
FATE_FFMPEG += $(FATE_EMBY_AC4)
