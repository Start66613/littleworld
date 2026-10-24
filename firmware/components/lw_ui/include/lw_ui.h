/* Little World - UI entry point shared by firmware and the Windows simulator.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Build the Little World shell and show the home screen (call with the LVGL lock held). */
void lw_ui_start(void);

#ifdef __cplusplus
}
#endif
