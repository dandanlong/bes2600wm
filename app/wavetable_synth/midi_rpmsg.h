#ifndef __APPS_WAVETABLE_SYNTH_MIDI_RPMSG_H
#define __APPS_WAVETABLE_SYNTH_MIDI_RPMSG_H

#include <stdint.h>

#define MIDI_RPMSG_ENDPOINT_NAME "rpmsg-usb-midi"
#define MIDI_RPMSG_MAGIC         0x4d494449u

enum midi_rpmsg_type_e
{
  MIDI_RPMSG_EVENT = 1,
  MIDI_RPMSG_ALL_OFF = 2
};

struct midi_rpmsg_msg_s
{
  uint32_t magic;
  uint8_t type;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

#endif
