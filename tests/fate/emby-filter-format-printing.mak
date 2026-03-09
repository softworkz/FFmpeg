# FATE tests for Emby filter format printing customizations
#
# These tests verify the behavior introduced by the following custom commits:
#   161 (39104a1ff5) - avfilter/avfilter: add avfilter_print_config_formats()
#   162 (6c49d352dd) - ftools/opt_common: Print filter input/output formats in help output

EMBY_FFMPEG_FMT = ffmpeg$(PROGSSUF)$(EXESUF)

# --- Test 1: Passthrough filter shows "All (passthrough)" (commit 161) ---
# The null filter uses FF_FILTER_FORMATS_PASSTHROUGH, so both input and output
# pads must show "Formats: All (passthrough)".
FATE_EMBY_FILTER_FMT-$(CONFIG_NULL_FILTER) += fate-emby-filter-fmt-passthrough
fate-emby-filter-fmt-passthrough: CMP = oneline
fate-emby-filter-fmt-passthrough: REF = 2
fate-emby-filter-fmt-passthrough: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=null 2>/dev/null | \
        grep -c "Formats: All (passthrough)"

# --- Test 2: Audio passthrough filter shows "All (passthrough)" (commit 161) ---
# The anull filter is the audio equivalent of null, also passthrough.
FATE_EMBY_FILTER_FMT-$(CONFIG_ANULL_FILTER) += fate-emby-filter-fmt-audio-passthrough
fate-emby-filter-fmt-audio-passthrough: CMP = oneline
fate-emby-filter-fmt-audio-passthrough: REF = 2
fate-emby-filter-fmt-audio-passthrough: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=anull 2>/dev/null | \
        grep -c "Formats: All (passthrough)"

# --- Test 3: Dynamic filter with defaults shows format list (commits 161+162) ---
# The overlay filter uses query_formats. The output must contain "Dynamic, Default:"
# followed by a bracketed format list for each of its 3 pads (2 inputs + 1 output).
FATE_EMBY_FILTER_FMT-$(CONFIG_OVERLAY_FILTER) += fate-emby-filter-fmt-dynamic-defaults
fate-emby-filter-fmt-dynamic-defaults: CMP = oneline
fate-emby-filter-fmt-dynamic-defaults: REF = 3
fate-emby-filter-fmt-dynamic-defaults: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=overlay 2>/dev/null | \
        grep -c "Formats: Dynamic, Default: \["

# --- Test 4: Dynamic filter without defaults shows just "Dynamic" (commit 161) ---
# The format filter uses query_formats but cannot produce defaults without
# arguments, so it shows just "Dynamic" (no "Default:" suffix).
FATE_EMBY_FILTER_FMT-$(CONFIG_FORMAT_FILTER) += fate-emby-filter-fmt-dynamic-only
fate-emby-filter-fmt-dynamic-only: CMP = oneline
fate-emby-filter-fmt-dynamic-only: REF = 2
fate-emby-filter-fmt-dynamic-only: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=format 2>/dev/null | \
        grep -cE "Formats: Dynamic$$"

# --- Test 5: Audio dynamic filter shows format list (commits 161+162) ---
# The volume filter is an audio filter with query_formats that produces defaults.
FATE_EMBY_FILTER_FMT-$(CONFIG_VOLUME_FILTER) += fate-emby-filter-fmt-audio-dynamic
fate-emby-filter-fmt-audio-dynamic: CMP = oneline
fate-emby-filter-fmt-audio-dynamic: REF = 2
fate-emby-filter-fmt-audio-dynamic: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=volume 2>/dev/null | \
        grep -c "Formats: Dynamic, Default: \["

# --- Test 6: "Formats:" keyword present in filter help (commit 162) ---
# Verifies that the show_help_filter integration appends ", Formats: " to every
# pad line. The overlay filter has 3 pads (2 in + 1 out).
FATE_EMBY_FILTER_FMT-$(CONFIG_OVERLAY_FILTER) += fate-emby-filter-fmt-keyword-present
fate-emby-filter-fmt-keyword-present: CMP = oneline
fate-emby-filter-fmt-keyword-present: REF = 3
fate-emby-filter-fmt-keyword-present: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=overlay 2>/dev/null | \
        grep -c "), Formats: "

# --- Test 7: Source filter shows "none (source filter)" for inputs (commit 162) ---
# The color filter is a source with no inputs, so "none (source filter)" must
# appear, and the output pad must have format info.
FATE_EMBY_FILTER_FMT-$(CONFIG_COLOR_FILTER) += fate-emby-filter-fmt-source
fate-emby-filter-fmt-source: CMP = oneline
fate-emby-filter-fmt-source: REF = 1
fate-emby-filter-fmt-source: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=color 2>/dev/null | \
        grep -c "none (source filter)"

# --- Test 8: Source filter output pad has format info (commits 161+162) ---
FATE_EMBY_FILTER_FMT-$(CONFIG_COLOR_FILTER) += fate-emby-filter-fmt-source-output
fate-emby-filter-fmt-source-output: CMP = oneline
fate-emby-filter-fmt-source-output: REF = 1
fate-emby-filter-fmt-source-output: CMD = run $(EMBY_FFMPEG_FMT) \
        -h filter=color 2>/dev/null | \
        grep -c "Formats: Dynamic, Default: \["

# --- Collect all tests ---
FATE_EMBY_FILTER_FMT += $(FATE_EMBY_FILTER_FMT-yes)
FATE_FFMPEG += $(FATE_EMBY_FILTER_FMT)
