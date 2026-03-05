#pragma once

#include "app_state.h"

void boot_screen_init(void);
void boot_screen_update(app_state_t state, const char *detail); // detail = IP or error msg
void boot_screen_destroy(void);
