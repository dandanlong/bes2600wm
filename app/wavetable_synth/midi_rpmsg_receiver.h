#ifndef __APPS_WAVETABLE_SYNTH_MIDI_RPMSG_RECEIVER_H
#define __APPS_WAVETABLE_SYNTH_MIDI_RPMSG_RECEIVER_H

struct pcm_samp_engine_s;

int midi_rpmsg_receiver_start(struct pcm_samp_engine_s *eng);
void midi_rpmsg_receiver_stop(void);

#endif
