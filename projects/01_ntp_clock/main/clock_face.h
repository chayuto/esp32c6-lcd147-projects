#pragma once

#include <time.h>
#include <stdbool.h>

void clock_face_init(void);
void clock_face_update(const struct tm *t);
void clock_face_apply_theme(void);    // called after theme change
void clock_face_set_wifi_status(bool connected);
void clock_face_set_ntp_status(bool synced);
