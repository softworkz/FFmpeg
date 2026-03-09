# FATE tests for Emby throttling customization
#
# These tests verify the behavior introduced by the following custom commit:
#   033 (84b9d40aea) - Implement throttling
#
# The commit adds:
#   - A global throttleMs variable (default 0 = disabled)
#   - Interactive 't'/'T' keyboard handler to set throttle value at runtime
#   - A throttle status field in the progress report output
#   - av_usleep-based delay in process_input() when throttleMs > 0

EMBY_FFMPEG = ffmpeg$(PROGSSUF)$(EXESUF)

# --- Test 1: Progress report contains throttle=off field (default state) ---
# The -progress output must include the "throttle=off" key-value pair when
# throttling is disabled (the default). This verifies the throttleMs variable
# is initialized to 0 and the progress reporting code is present.
# We use grep -m1 to extract just the first matching line.
FATE_EMBY_THROTTLE += fate-emby-throttle-progress-off
fate-emby-throttle-progress-off: CMP = oneline
fate-emby-throttle-progress-off: REF = throttle=off
fate-emby-throttle-progress-off: CMD = run $(EMBY_FFMPEG) -nostdin \
        -f lavfi -i "sine=frequency=440:duration=0.1" \
        -acodec pcm_s16le -flags +bitexact -fflags +bitexact \
        -f null -progress pipe:1 /dev/null 2>/dev/null | \
        grep -m1 "^throttle="

# --- Test 2: Stderr progress line contains throttle= field ---
# The human-readable stderr progress line must include "throttle=off" when
# throttling is disabled. This tests the buf (stderr) reporting path as
# opposed to the buf_script (-progress) path tested above.
FATE_EMBY_THROTTLE += fate-emby-throttle-stderr-field
fate-emby-throttle-stderr-field: CMP = oneline
fate-emby-throttle-stderr-field: REF = 1
fate-emby-throttle-stderr-field: CMD = run $(EMBY_FFMPEG) -nostdin \
        -f lavfi -i "color=c=black:s=16x16:d=0.1:r=5" \
        -vcodec rawvideo -flags +bitexact -fflags +bitexact \
        -f null /dev/null 2>&1 | \
        grep -c "throttle=off"

# --- Test 3: Help text includes throttle key binding ---
# The keyboard help (triggered by '?' during interactive mode) must list the
# 't' key for throttling. We verify the help string is compiled into the
# ffmpeg binary by searching it with strings(1).
FATE_EMBY_THROTTLE += fate-emby-throttle-help-text
fate-emby-throttle-help-text: CMP = oneline
fate-emby-throttle-help-text: REF = 1
fate-emby-throttle-help-text: CMD = strings $(TARGET_PATH)/$(EMBY_FFMPEG) | \
        grep -c "set throttling"

# --- Test 4: Progress output contains throttle field between drop_frames and speed ---
# Verifies the throttle field appears in the correct structural position within
# the -progress output block: immediately after drop_frames and before speed.
FATE_EMBY_THROTTLE += fate-emby-throttle-progress-position
fate-emby-throttle-progress-position: CMP = oneline
fate-emby-throttle-progress-position: REF = throttle=off
fate-emby-throttle-progress-position: CMD = run $(EMBY_FFMPEG) -nostdin \
        -f lavfi -i "color=c=black:s=16x16:d=0.1:r=5" \
        -vcodec rawvideo -flags +bitexact -fflags +bitexact \
        -f null -progress pipe:1 /dev/null 2>/dev/null | \
        grep -A1 "^drop_frames=" | grep -m1 "^throttle="

# --- Collect all tests ---
FATE_FFMPEG += $(FATE_EMBY_THROTTLE)
