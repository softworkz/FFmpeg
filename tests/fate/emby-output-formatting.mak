# FATE tests for Emby output formatting customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   042 (f4c2420b5f) - Suppress printing metadata tags with underscore prefix
#   044 (90b1ca5d26) - Suppress printing of chapter information
#   116 (acddcb40b8) - Update program info (copyright, config, lib versions)
#   040 (b4f0d9490f) - Do not dump extra data for attachment streams
#   062 (81dff56c0d) - Don't print certain side data (gated on LC_ALL)

EMBY_UNDERSCORE_META_FILE = tests/data/emby-underscore-meta.nut
EMBY_CHAPTERS_FILE = tests/data/emby-chapters.mkv
EMBY_ATTACHMENT_FILE = tests/data/emby-attachment.mkv
EMBY_SIDEDATA_FILE = tests/data/emby-sidedata.ogg

EMBY_FFMPEG = ffmpeg$(PROGSSUF)$(EXESUF)
EMBY_FFPROBE = ffprobe$(PROGSSUF)$(EXESUF)

# --- Test data generation ---

tests/data/emby-underscore-meta.nut: $(EMBY_FFMPEG) $(AREF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 0.01 -metadata _hidden=secret -metadata visible=yes \
        -acodec pcm_s16le -flags +bitexact -fflags +bitexact \
        -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-chapters.mkv: $(EMBY_FFMPEG) $(AREF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -f ffmetadata -i $(SRC_PATH)/tests/emby-chapters.ffmeta \
        -map 0:a -map_metadata 1 -map_chapters 1 -t 0.1 \
        -acodec pcm_s16le -flags +bitexact -fflags +bitexact \
        -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-attachment.mkv: $(EMBY_FFMPEG) $(AREF) | tests/data
	$(M)printf 'EMBY_TEST_ATTACHMENT_DATA\n' > tests/data/emby-attachment-payload.txt
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -attach $(TARGET_PATH)/tests/data/emby-attachment-payload.txt \
        -metadata:s:1 mimetype=text/plain -metadata:s:1 filename=testfile.txt \
        -t 0.01 -acodec pcm_s16le -flags +bitexact -fflags +bitexact \
        -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-sidedata.ogg: $(EMBY_FFMPEG) $(AREF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f s16le -ar 44100 -ac 1 -i $(TARGET_PATH)/$(AREF) \
        -t 0.1 -c:a libopus -b:a 32k -flags +bitexact \
        -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/emby-underscore-meta.nut tests/data/emby-chapters.mkv tests/data/emby-attachment.mkv tests/data/emby-sidedata.ogg: TAG = GEN

# --- Test 1a: File has both underscore and visible metadata (commit 042, precondition) ---
# Proves the test file contains _hidden and visible tags via ffprobe.
FATE_EMBY_OUTPUT-$(CONFIG_NUT_DEMUXER) += fate-emby-ffprobe-underscore-metadata-exists
fate-emby-ffprobe-underscore-metadata-exists: $(EMBY_UNDERSCORE_META_FILE)
fate-emby-ffprobe-underscore-metadata-exists: CMD = run $(EMBY_FFPROBE) -bitexact \
        -show_entries format_tags -of compact \
        $(TARGET_PATH)/$(EMBY_UNDERSCORE_META_FILE) 2>/dev/null

# --- Test 1b: Underscore metadata hidden in av_dump_format (commit 042) ---
# Despite the file having _hidden tag, it must not appear in the format dump.
FATE_EMBY_OUTPUT-$(CONFIG_NUT_DEMUXER) += fate-emby-dump-no-underscore-metadata
fate-emby-dump-no-underscore-metadata: $(EMBY_UNDERSCORE_META_FILE)
fate-emby-dump-no-underscore-metadata: CMP = oneline
fate-emby-dump-no-underscore-metadata: REF = 0
fate-emby-dump-no-underscore-metadata: CMD = run $(EMBY_FFMPEG) -nostdin \
        -i $(TARGET_PATH)/$(EMBY_UNDERSCORE_META_FILE) -f null /dev/null 2>&1 | \
        grep -c _hidden || true

# --- Test 2a: File has chapters (commit 044, precondition) ---
# Proves the test file contains chapter data via ffprobe.
FATE_EMBY_OUTPUT-$(call ALLYES, MATROSKA_DEMUXER PCM_S16LE_DECODER) += fate-emby-ffprobe-chapters-exist
fate-emby-ffprobe-chapters-exist: $(EMBY_CHAPTERS_FILE)
fate-emby-ffprobe-chapters-exist: CMP = oneline
fate-emby-ffprobe-chapters-exist: REF = 1
fate-emby-ffprobe-chapters-exist: CMD = probechapters \
        $(TARGET_PATH)/$(EMBY_CHAPTERS_FILE) 2>/dev/null | \
        grep -c "^\[CHAPTER\]"

# --- Test 2b: Chapter info suppressed in av_dump_format (commit 044) ---
# The file has chapters but they must not appear in the format dump.
FATE_EMBY_OUTPUT-$(call ALLYES, MATROSKA_DEMUXER PCM_S16LE_DECODER) += fate-emby-dump-no-chapters
fate-emby-dump-no-chapters: $(EMBY_CHAPTERS_FILE)
fate-emby-dump-no-chapters: CMP = oneline
fate-emby-dump-no-chapters: REF = 0
fate-emby-dump-no-chapters: CMD = run $(EMBY_FFMPEG) -nostdin \
        -i $(TARGET_PATH)/$(EMBY_CHAPTERS_FILE) -f null /dev/null 2>&1 | \
        grep -c "Chapters:" || true

# --- Test 3: Copyright string includes Emby LLC (commit 116) ---
FATE_EMBY_OUTPUT += fate-emby-copyright
fate-emby-copyright: CMP = oneline
fate-emby-copyright: REF = Emby LLC
fate-emby-copyright: CMD = run $(EMBY_FFPROBE) -show_program_version -of default 2>/dev/null | \
        grep copyright | grep -o "Emby LLC"

# --- Test 4: Banner does not show configuration line (commit 116) ---
FATE_EMBY_OUTPUT-$(CONFIG_NUT_DEMUXER) += fate-emby-banner-no-config
fate-emby-banner-no-config: $(EMBY_UNDERSCORE_META_FILE)
fate-emby-banner-no-config: CMP = oneline
fate-emby-banner-no-config: REF = 0
fate-emby-banner-no-config: CMD = run $(EMBY_FFMPEG) -nostdin \
        -i $(TARGET_PATH)/$(EMBY_UNDERSCORE_META_FILE) -f null /dev/null 2>&1 | \
        grep -c "configuration:" || true

# --- Test 5: Banner does not show library versions (commit 116) ---
FATE_EMBY_OUTPUT-$(CONFIG_NUT_DEMUXER) += fate-emby-banner-no-libversions
fate-emby-banner-no-libversions: $(EMBY_UNDERSCORE_META_FILE)
fate-emby-banner-no-libversions: CMP = oneline
fate-emby-banner-no-libversions: REF = 0
fate-emby-banner-no-libversions: CMD = run $(EMBY_FFMPEG) -nostdin \
        -i $(TARGET_PATH)/$(EMBY_UNDERSCORE_META_FILE) -f null /dev/null 2>&1 | \
        grep -cE "^  lib(av|sw|post)" || true

# --- Test 6: No extradata dump for attachment streams (commit 040) ---
# With -show_data, the attachment has extradata_size > 0 but no hex dump should appear.
FATE_EMBY_OUTPUT-$(CONFIG_MATROSKA_DEMUXER) += fate-emby-ffprobe-no-attachment-extradata
fate-emby-ffprobe-no-attachment-extradata: $(EMBY_ATTACHMENT_FILE)
fate-emby-ffprobe-no-attachment-extradata: CMP = oneline
fate-emby-ffprobe-no-attachment-extradata: REF = 0
fate-emby-ffprobe-no-attachment-extradata: CMD = run $(EMBY_FFPROBE) -bitexact \
        -show_data -show_streams -select_streams t \
        $(TARGET_PATH)/$(EMBY_ATTACHMENT_FILE) 2>/dev/null | \
        grep -c "^extradata=" || true

# --- Test 7: Side data shown when LC_ALL is set (commit 062) ---
# FATE sets LC_ALL=C by default, so side data should be printed.
FATE_EMBY_OUTPUT-$(CONFIG_OGG_DEMUXER) += fate-emby-ffprobe-sidedata-with-lcall
fate-emby-ffprobe-sidedata-with-lcall: $(EMBY_SIDEDATA_FILE)
fate-emby-ffprobe-sidedata-with-lcall: CMP = oneline
fate-emby-ffprobe-sidedata-with-lcall: REF = 2
fate-emby-ffprobe-sidedata-with-lcall: CMD = run $(EMBY_FFPROBE) -bitexact \
        -show_packets -select_streams a -of compact \
        $(TARGET_PATH)/$(EMBY_SIDEDATA_FILE) 2>/dev/null | \
        grep -c side_data_type

# --- Test 8: Side data hidden when LC_ALL is not set (commit 062) ---
# Unsetting LC_ALL should suppress packet side data printing.
FATE_EMBY_OUTPUT-$(CONFIG_OGG_DEMUXER) += fate-emby-ffprobe-sidedata-without-lcall
fate-emby-ffprobe-sidedata-without-lcall: $(EMBY_SIDEDATA_FILE)
fate-emby-ffprobe-sidedata-without-lcall: CMP = oneline
fate-emby-ffprobe-sidedata-without-lcall: REF = 0
fate-emby-ffprobe-sidedata-without-lcall: CMD = unset LC_ALL && \
        run $(EMBY_FFPROBE) -bitexact \
        -show_packets -select_streams a -of compact \
        $(TARGET_PATH)/$(EMBY_SIDEDATA_FILE) 2>/dev/null | \
        grep -c side_data_type || true

# --- Collect all tests ---
FATE_EMBY_OUTPUT += $(FATE_EMBY_OUTPUT-yes)
FATE_FFMPEG += $(FATE_EMBY_OUTPUT)
