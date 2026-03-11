AVPROGS2-$(CONFIG_FFDETECT) += ffdetect

AVPROGS2     := $(AVPROGS2-yes:%=%$(PROGSSUF)$(EXESUF))
PROGS       += $(AVPROGS2)

AVBASENAMES2  = ffdetect
ALLAVPROGS   = $(AVBASENAMES2:%=%$(PROGSSUF)$(EXESUF))
ALLAVPROGS2_G = $(AVBASENAMES2:%=%$(PROGSSUF)_g$(EXESUF))

OBJS-ffdetect                        += ffdetect/outputwriters.o
OBJS-ffdetect                        += fftools/textformat/avtextformat.o
OBJS-ffdetect                        += fftools/textformat/tf_compact.o
OBJS-ffdetect                        += fftools/textformat/tf_default.o
OBJS-ffdetect                        += fftools/textformat/tf_flat.o
OBJS-ffdetect                        += fftools/textformat/tf_ini.o
OBJS-ffdetect                        += fftools/textformat/tf_json.o
OBJS-ffdetect                        += fftools/textformat/tf_mermaid.o
OBJS-ffdetect                        += fftools/textformat/tf_xml.o
OBJS-ffdetect                        += fftools/textformat/tw_avio.o
OBJS-ffdetect                        += fftools/textformat/tw_buffer.o
OBJS-ffdetect                        += fftools/textformat/tw_stdout.o
OBJS-ffdetect-$(CONFIG_CUVID)        += ffdetect/ffdetect_nv.o
OBJS-ffdetect-$(CONFIG_LIBMFX)       += ffdetect/ffdetect_qsv.o
OBJS-ffdetect-$(CONFIG_AMF)          += ffdetect/ffdetect_amf.o
OBJS-ffdetect-$(HAVE_VAAPI_DRM)      += ffdetect/device_lookup.o
OBJS-ffdetect-$(HAVE_VAAPI_DRM)      += ffdetect/ffdetect_vaapi.o

define DOFFTOOL1
OBJS-$(1) += ffdetect/detectutils.o ffdetect/$(1).o $(OBJS-$(1)-yes)
$(1)$(PROGSSUF)_g$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): | ffdetect fftools/textformat
$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): LDFLAGS += $(LDFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): FF_EXTRALIBS += $(EXTRALIBS-$(1))
-include $$(OBJS-$(1):.o=.d)
endef

$(foreach P,$(AVPROGS2-yes),$(eval $(call DOFFTOOL1,$(P))))

all: $(AVPROGS2)

ffdetect/ffdetect.o ffdetect/detectutils.o: libavutil/ffversion.h | ffdetect
OUTDIRS += ffdetect

ifdef AVPROGS2
install: install-progs2 install-data
endif

install-progs2-yes:
install-progs2-$(CONFIG_SHARED): install-libs

install-progs2: install-progs2-yes $(AVPROGS2)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(AVPROGS2) "$(BINDIR)"

uninstall: uninstall-progs2

uninstall-progs2:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLAVPROGS2))

clean::
	$(RM) $(ALLAVPROGS2) $(ALLAVPROGS2_G) $(CLEANSUFFIXES:%=ffdetect/%)
