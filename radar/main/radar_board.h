#pragma once

/* Super Mini outer headers do not expose GPIO41/GPIO42. Keep the 16 MB
 * transmitter pins unchanged for that board. */

#if defined(RADAR_BOARD_SUPERMINI)
enum {
    RADAR_LD2450_TX_GPIO = 5,
    RADAR_LD2450_RX_GPIO = 4,
};
#else
enum {
    RADAR_LD2450_TX_GPIO = 42,
    RADAR_LD2450_RX_GPIO = 41,
};
#endif
