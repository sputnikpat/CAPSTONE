/*
 * bms_status.h — neutral, read-only snapshot of the BMS for display and
 *                telemetry.
 *
 *  M1 Step 4.
 *
 *  WHY THIS FILE EXISTS
 *  The OLED and UART code must not know HOW the BMS decides anything.
 *  They only render a snapshot. The application layer fills it in:
 *    - Step 4: main.c fills it from sensors + commanded switch states.
 *    - Step 5: the safety layer fills fault_flags / fault_label.
 *    - Step 6: the reconfiguration module fills topo_label /
 *              topo_confirmed.
 *  Because topology and fault arrive as plain text labels, the display
 *  never includes reconfig.h and never sees the reconfiguration state
 *  enum or transition rules (IP guardrail).
 *
 *  UNITS: integers only, mV and mA. Rounding happens once, where the
 *  float from the INA260 driver is converted (main.c), so display and
 *  telemetry never need float printf.
 */

#ifndef BMS_STATUS_H
#define BMS_STATUS_H

#include <stdint.h>

#define BMS_STR_A      0u
#define BMS_STR_B      1u
#define BMS_NUM_STR    2u

/* One string's sensor snapshot. */
typedef struct {
    uint8_t  valid;        /* 1 = BOTH V and I were read OK THIS cycle.
                              0 = v_mV / i_mA are stale and must NOT be
                              shown or logged as live values.           */
    int32_t  v_mV;         /* INA260 bus voltage, mV                     */
    int32_t  i_mA;         /* INA260 current, mA, signed (+ = IN+->IN-)  */
    uint32_t last_ok_ms;   /* HAL tick of the last fully good read       */
    uint32_t fail_count;   /* copy of INA260.fail_count (diagnostics)    */
} BmsStringSnap;

typedef struct {
    /* timing / liveness */
    uint32_t tick_ms;          /* HAL_GetTick() at sample time            */
    uint32_t loop_count;       /* heartbeat: must keep moving on screen   */
    uint32_t overruns;         /* loop iterations that missed their slot  */
    uint32_t work_us_max;      /* worst loop work time seen, µs (DWT)     */
    uint32_t oled_us_max;      /* worst single OLED_Service() call, µs    */

    /* sensing */
    BmsStringSnap str[BMS_NUM_STR];

    /* switches — COMMANDED state (GPIO ODR), not proven MOSFET state */
    uint8_t  sw_a;
    uint8_t  sw_b;
    uint8_t  sw_load;

    /* topology — label text supplied by whichever layer owns topology.
       Keep <= 6 characters. */
    const char *topo_label;
    uint8_t     topo_confirmed; /* 0 = derived from commands only ("CMD")
                                   1 = confirmed by sensor readback      */

    /* faults — bit meanings are defined by the safety layer (Step 5).
       Step 4 uses only the sensor-loss latch set in main.c. */
    uint32_t    fault_flags;    /* 0 = no fault                          */
    const char *fault_label;    /* <= 14 characters                      */

    /* misc */
    uint8_t  motor_on;
    uint32_t i2c1_recover;      /* bus_i2c1.recover_count                */
    uint32_t i2c3_recover;      /* bus_i2c3.recover_count                */
    uint32_t oled_err;          /* OLED write failures (isolated)        */
} BmsStatus;

#endif /* BMS_STATUS_H */
