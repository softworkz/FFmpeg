FATE_HWCONTEXT += fate-hwdevice
fate-hwdevice: libavutil/tests/hwdevice$(EXESUF)
fate-hwdevice: CMD = run libavutil/tests/hwdevice$(EXESUF)
fate-hwdevice: CMP = null

FATE_HWCONTEXT += fate-hwdevice_ctx_derive
fate-hwdevice_ctx_derive: libavutil/tests/hwdevice_ctx_derive$(EXESUF)
fate-hwdevice_ctx_derive: CMD = run libavutil/tests/hwdevice_ctx_derive$(EXESUF)
fate-hwdevice_ctx_derive: CMP = null

FATE_HW-$(CONFIG_AVUTIL) += $(FATE_HWCONTEXT)
