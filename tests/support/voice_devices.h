#ifndef PAPO_TEST_VOICE_DEVICES_H
#define PAPO_TEST_VOICE_DEVICES_H
#include "config/audio.h"
#include <glib.h>
/* In-process GstDeviceProvider fixture: never opens a physical audio device. */
void test_voice_devices_register(void);
gint test_voice_audible_buffers(void);
void test_voice_devices_select(audio_config *config, const json_t *devices);
#endif
