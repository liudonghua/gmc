#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gmc_audio_control_set_volume(int volume);
void gmc_audio_control_adjust_volume(int delta);
void gmc_audio_control_set_muted(bool muted);
void gmc_audio_control_toggle_mute(void);
void gmc_audio_control_set_paused(bool paused);

bool gmc_audio_control_is_muted(void);
bool gmc_audio_control_is_paused(void);
int gmc_audio_control_get_volume(void);

#ifdef __cplusplus
}
#endif
