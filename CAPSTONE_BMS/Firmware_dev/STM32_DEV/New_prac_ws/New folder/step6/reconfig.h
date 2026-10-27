/*
 * reconfig.h — string-level reconfiguration state machine
 *
 *  M1 Step 6. PROJECT-SPECIFIC CONTRIBUTION. Kept out of every generic
 *  module: bms_switch / bms_safety / drivers / display / telemetry do not
 *  include this header. Display and telemetry receive only a text label
 *  and a confirmed bit through BmsStatus.
 *
 *  LAYERING (strict)
 *    bms_safety   runs first every loop, may OPEN anything at any time,
 *                 is the only thing that latches faults.
 *    reconfig     runs right after safety in the same loop. It is the
 *                 ONLY code that CLOSES a switch, and only if
 *                 Safety_BlockMask() allows it at that moment.
 *    bms_switch   pins only.
 *
 *  WHAT IT GUARANTEES (each one is a host-test assertion, STEP6_NOTES §8)
 *    G1  Never commands A and B on together unless the dV gate passed on
 *        the samples immediately before the close (and only when
 *        RCF_PARALLEL_ENABLED).
 *    G2  Break-before-make on every string change: the old string is
 *        confirmed OPEN by current readback before the new one closes.
 *    G3  Never closes a switch that safety blocks.
 *    G4  At least RCF_COOLDOWN_MS between topology changes.
 *    G5  Any state it cannot explain (switch moved by someone else,
 *        readback failure, nothing left to run on) -> LOCKOUT, all off,
 *        until MCU reset. Same reset-only policy as the safety layer.
 *
 *  NOT IN THIS STEP: precharge (not on the PCB), charging, SOC-driven
 *  split / rejoin / alternation (Step 7 provides SOC).
 */

#ifndef RECONFIG_H
#define RECONFIG_H

#include <stdint.h>
#include "bms_status.h"

/* Event bits returned by Reconfig_Step() */
#define RCF_EVT_CHANGED     (1u << 0)  /* topology changed this cycle        */
#define RCF_EVT_LOAD_LOST   (1u << 1)  /* load released: stop the motors     */
#define RCF_EVT_LOCKOUT     (1u << 2)  /* entered LOCKOUT this cycle         */

/** Call once after Safety_Init(), before the loop. All switches off. */
void Reconfig_Init(uint32_t now_ms);

/**
 * @brief  One step of the state machine. Call every loop, immediately
 *         after Safety_Step(), with the same snapshot.
 * @return RCF_EVT_* bits for this cycle (0 = nothing happened).
 */
uint8_t Reconfig_Step(const BmsStringSnap snap[BMS_NUM_STR], uint32_t now_ms);

/** 1 while the motors may be commanded: a string is on the load, OR the
 *  old string is being proven open in a swap (the load must stay applied
 *  for that proof; see reconfig.c bookkeeping). main.c stops the motors
 *  when this goes to 0 and on RCF_EVT_LOAD_LOST. */
uint8_t Reconfig_MotorsAllowed(void);

/** <= 6 chars for OLED row 3 / CSV: OFF A B A+B SWAP WAIT LOCK */
const char *Reconfig_TopoLabel(void);

/** 1 = current readback matches the topology right now (CSV topo_conf). */
uint8_t Reconfig_TopoConfirmed(void);

/** Last event line (for UART), or NULL if none since the last call. */
const char *Reconfig_TakeMessage(void);

#endif /* RECONFIG_H */
