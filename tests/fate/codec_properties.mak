CODEC_PROPS_TEST_FILE = tests/data/ffprobe-test.nut

FATE_CODEC_PROPERTIES-$(CONFIG_AVDEVICE) += fate-ffprobe_codec_properties
fate-ffprobe_codec_properties: $(CODEC_PROPS_TEST_FILE)
fate-ffprobe_codec_properties: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=index,codec_type,closed_captions,film_grain -bitexact -of default $(TARGET_PATH)/$(CODEC_PROPS_TEST_FILE)

FATE_FFPROBE += $(FATE_CODEC_PROPERTIES-yes)
