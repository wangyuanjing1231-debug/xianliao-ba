/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (see LICENSE-muse-gadget-sdk.txt).
 *
 * Trimmed from facebookincubator/muse-gadget-sdk esp32/components/muse/muse_state.h:
 * only the mode enum that muse_pixel.c needs.
 */
#pragma once

typedef enum {
    MUSE_MODE_BOOT = 0,
    MUSE_MODE_IDLE,
    MUSE_MODE_LISTENING,
    MUSE_MODE_THINKING,
    MUSE_MODE_SPEAKING,
    MUSE_MODE_ERROR,
    MUSE_MODE_OFF,       /* powering down */
    MUSE_MODE_COUNT,
} muse_mode_t;
