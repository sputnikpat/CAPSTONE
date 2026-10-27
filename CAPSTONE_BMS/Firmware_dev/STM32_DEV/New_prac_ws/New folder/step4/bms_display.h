/*
 * bms_display.h — BMS screens for the 128x64 OLED
 *
 *  M1 Step 4. Renders a BmsStatus snapshot into the OLED framebuffer.
 *  Does NOT touch I2C: Display_Render() only draws and calls
 *  OLED_Commit(); the bytes go out page-by-page from OLED_Service() in
 *  the main loop.
 *
 *  Knows nothing about how topology or faults are decided — it prints
 *  the labels it is given (see bms_status.h).
 */

#ifndef BMS_DISPLAY_H
#define BMS_DISPLAY_H

#include "bms_status.h"
#include <stddef.h>

/** Boot / sensor-probe screen with the column-offset border.
 *  Blocking flush (boot only, switches are off). */
void Display_Boot(uint8_t sensor_a_ok, uint8_t sensor_b_ok);

/** Main BMS screen. RAM only + commit; non-blocking. */
void Display_Render(const BmsStatus *s);

/* Integer formatters, exposed so telemetry and host tests can reuse them.
 * Both round half away from zero to 10 mV / 10 mA. */
void Display_FmtVolt(char *out, size_t n, int32_t mV);  /* "11.12"  */
void Display_FmtAmp (char *out, size_t n, int32_t mA);  /* "+0.85" / "-1.20" */

#endif /* BMS_DISPLAY_H */
