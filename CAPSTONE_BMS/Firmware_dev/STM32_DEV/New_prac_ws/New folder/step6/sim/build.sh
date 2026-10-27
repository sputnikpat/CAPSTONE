#!/bin/sh
# Host build of the UNMODIFIED firmware sources in two configurations.
set -e
for F in 0 1; do
  mkdir -p cfg$F
  sed "s/^#define SAFETY_VBUS_SEES_OPEN_STRING    0/#define SAFETY_VBUS_SEES_OPEN_STRING    $F/" ../base/bms_safety_cfg.h > cfg$F/bms_safety_cfg.h
  gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O1 \
      -I. -Icfg$F -I.. -I../base \
      ../reconfig.c ../base/bms_safety.c sim_switch.c sim_test.c -lm -o sim$F
done
