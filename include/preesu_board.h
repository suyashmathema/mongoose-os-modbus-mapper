#pragma once

#include "mgos.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_EVENT_BASE MGOS_EVENT_BASE('B', 'R', 'D')

enum board_event {
    BOARD_OUTPUT_CHANGED = BOARD_EVENT_BASE,
    BOARD_PULSE_STARTED,
    BOARD_PULSE_FINISHED,
    BOARD_INPUT_STATE_REQUESTED,
    BOARD_TELEMETRY_REQUESTED,
    BOARD_ATTRIBUTE_REQUESTED,
};

#ifdef __cplusplus
}
#endif