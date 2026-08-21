# ##############################################################################
# apps/wavetable_synth/CMakeLists.txt
# ##############################################################################

if(CONFIG_WAVETABLE_SYNTH)
  nuttx_add_application(
    NAME
    ${CONFIG_WAVETABLE_SYNTH_PROGNAME}
    PRIORITY
    ${CONFIG_WAVETABLE_SYNTH_PRIORITY}
    STACKSIZE
    ${CONFIG_WAVETABLE_SYNTH_STACKSIZE}
    MODULE
    ${CONFIG_WAVETABLE_SYNTH}
    SRCS
    wavetable_synth_main.c
    wt_engine.c
    wt_sine_table.c
    pcm64_engine.c
    pcm_sample_engine.c
    midi_rpmsg_receiver.c)
endif()


if(CONFIG_USB_MIDI_BRIDGE)
  nuttx_add_application(
    NAME
    ${CONFIG_USB_MIDI_BRIDGE_PROGNAME}
    PRIORITY
    ${CONFIG_USB_MIDI_BRIDGE_PRIORITY}
    STACKSIZE
    ${CONFIG_USB_MIDI_BRIDGE_STACKSIZE}
    MODULE
    ${CONFIG_USB_MIDI_BRIDGE}
    SRCS
    usb_midi_bridge.c)
endif()
