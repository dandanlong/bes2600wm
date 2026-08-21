############################################################################
# apps/wavetable_synth/Makefile
############################################################################

include $(APPDIR)/Make.defs

ifneq ($(CONFIG_WAVETABLE_SYNTH),)

PROGNAME  = $(CONFIG_WAVETABLE_SYNTH_PROGNAME)
PRIORITY  = $(CONFIG_WAVETABLE_SYNTH_PRIORITY)
STACKSIZE = $(CONFIG_WAVETABLE_SYNTH_STACKSIZE)
MODULE    = $(CONFIG_WAVETABLE_SYNTH)

MAINSRC = wavetable_synth_main.c
CSRCS   = wt_engine.c wt_sine_table.c pcm64_engine.c pcm_sample_engine.c \
          fx_psram_pool.c xmos_fx_chain.c midi_rpmsg_receiver.c lc3_bench.c \
          $(TOPDIR)/../external/lc3/lc3/src/attdet.c \
          $(TOPDIR)/../external/lc3/lc3/src/bits.c \
          $(TOPDIR)/../external/lc3/lc3/src/bwdet.c \
          $(TOPDIR)/../external/lc3/lc3/src/energy.c \
          $(TOPDIR)/../external/lc3/lc3/src/lc3.c \
          $(TOPDIR)/../external/lc3/lc3/src/ltpf.c \
          $(TOPDIR)/../external/lc3/lc3/src/mdct.c \
          $(TOPDIR)/../external/lc3/lc3/src/plc.c \
          $(TOPDIR)/../external/lc3/lc3/src/sns.c \
          $(TOPDIR)/../external/lc3/lc3/src/spec.c \
          $(TOPDIR)/../external/lc3/lc3/src/tables.c \
          $(TOPDIR)/../external/lc3/lc3/src/tns.c \
          xmos_fx/global.c xmos_fx/VoxAC30.c xmos_fx/Marshall800.c \
          xmos_fx/MesaBoogie.c xmos_fx/FenderTwinReverb.c \
          xmos_fx/DiezelCH4.c xmos_fx/Vox212.c xmos_fx/Mesa412.c \
          xmos_fx/Marshall212.c xmos_fx/Marshall412.c \
          xmos_fx/FenderTwinReverb212.c xmos_fx/Diezel412.c \
          xmos_fx/Ts808.c xmos_fx/MxrDisPlus.c \
          xmos_fx/Vintage_ProcoRat.c xmos_fx/CompressorA.c \
          xmos_fx/chorus.c xmos_fx/pureDelay.c xmos_fx/EQT1.c \
          xmos_fx/reverbT1.c xmos_fx/Tremolo.c xmos_fx/autoPan.c \
          xmos_fx/multiBandCompT0.c xmos_fx/xmos_lut.c

# Dig FatFs FIL (ff.h) to map bank file -> LBA for raw SD streaming.
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/fs/fatfs
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/fs/fatfs/fatfs/source
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/wavetable_synth/xmos_fx
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../external/lc3/lc3/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../external/lc3/lc3/src
CFLAGS += -ffast-math

endif

ifneq ($(CONFIG_USB_MIDI_BRIDGE),)

PROGNAME  = $(CONFIG_USB_MIDI_BRIDGE_PROGNAME)
PRIORITY  = $(CONFIG_USB_MIDI_BRIDGE_PRIORITY)
STACKSIZE = $(CONFIG_USB_MIDI_BRIDGE_STACKSIZE)
MODULE    = $(CONFIG_USB_MIDI_BRIDGE)

MAINSRC = usb_midi_bridge.c
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/bes/framework/services/platform/hal
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/bes/framework/services/platform/drivers/usb/usb_dev/inc

endif

include $(APPDIR)/Application.mk
