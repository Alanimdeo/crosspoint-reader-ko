#pragma once

#include <BoardConfig.h>

#include <cstdint>

// Loads a cell's capacity into a BQ27220 whose Design Capacity reads TI's 3000 mAh default (it returns there whenever
// the gauge loses power) or whose FullChargeCapacity() reads more than a quarter above it. A gauge counting a small
// cell against 3000 mAh falls far too slowly, then drops to single digits when the voltage gives out.
//
// One short step per call, about 8 s in all: call from the task that owns the gauge until it returns false. The last
// call of a load logs what the gauge reads back; a gauge that needed nothing logs nothing.
//
// Port of BatteryMonitor::loadDesignCapacity() from freeink-sdk e040e9a. Remove it once the SDK pin reaches that
// commit.
bool loadBq27220DesignCapacity(const BoardConfig::BatteryGaugeConfig& gauge, uint16_t mah);
