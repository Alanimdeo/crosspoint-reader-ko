#include "Bq27220Capacity.h"

#include <Arduino.h>
#include <Logging.h>
#include <Wire.h>

namespace {

// TRM SLUUBD4A 6.1: unseal, full access, CONFIG UPDATE, checksummed Data Memory
// writes, exit with reinit, seal. The X3's gauge ignores keys sent back to back,
// so they go 1.5 s apart.
constexpr uint8_t BQ27220_CONTROL = 0x00;
constexpr uint8_t BQ27220_FULL_CHARGE_CAPACITY = 0x12;
constexpr uint8_t BQ27220_OPERATION_STATUS = 0x3A;  // CFGUPDATE bit 10, SEC[1:0] bits 2:1
constexpr uint8_t BQ27220_DESIGN_CAPACITY = 0x3C;
constexpr uint8_t BQ27220_MAC_CONTROL = 0x3E;
constexpr uint8_t BQ27220_MAC_DATA = 0x40;
constexpr uint8_t BQ27220_MAC_DATA_SUM = 0x60;  // MACDataLen() in the high byte
constexpr uint16_t BQ27220_DM_LEARNED_FCC = 0x929D;
constexpr uint16_t BQ27220_DM_DESIGN_CAPACITY = 0x929F;
// Unseal, full access, ENTER_CFG_UPDATE.
constexpr uint16_t BQ27220_UNLOCK[] = {0x0414, 0x3672, 0xFFFF, 0xFFFF, 0x0090};

struct Bq27220Load {
  uint8_t step = 0;  // 0 check, 1-5 BQ27220_UNLOCK, 6 Learned FCC, 7 Design Capacity, 8 exit, 9 done
  bool cfg = false;
  bool wrote = false;
  bool reportDue = false;  // the load went past the first check
  unsigned long at = 0;
  unsigned long since = 0;
};
Bq27220Load bq27220Load;

bool readReg16(const uint8_t addr, const uint8_t reg, uint16_t& out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) return false;
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  out = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
  return true;
}

// One I2C write per word: the X3's BQ27220 ignores a Control() subcommand split in two.
bool writeReg16(const uint8_t addr, const uint8_t reg, const uint16_t val) {
  delayMicroseconds(66);  // BQ27220 bus-free time between transactions
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(val & 0xFF));
  Wire.write(static_cast<uint8_t>(val >> 8));
  return Wire.endTransmission(true) == 0;
}

// A learning cycle moves Learned Full Charge Capacity at most 256 mAh down (TRM
// 1.1.3), so one learned against TI's 3000 mAh default stays far above a small cell.
bool bq27220TooHigh(const uint16_t learned, const uint16_t mah) { return learned > mah + mah / 4; }

// Replaces TI's default Design Capacity, or a Learned FCC that is too high, in one
// big-endian word, adjusting the block checksum by the bytes that change.
bool bq27220WriteParam(const uint8_t addr, const uint16_t address, const uint16_t mah) {
  uint16_t old = 0;
  uint16_t sum = 0;
  if (!writeReg16(addr, BQ27220_MAC_CONTROL, address)) return false;
  delay(10);
  if (!readReg16(addr, BQ27220_MAC_DATA, old) || !readReg16(addr, BQ27220_MAC_DATA_SUM, sum)) return false;
  const uint16_t value = __builtin_bswap16(old);
  if (address == BQ27220_DM_LEARNED_FCC ? !bq27220TooHigh(value, mah) : value != 3000) return true;
  bq27220Load.wrote = true;
  sum = (sum & 0xFF00) | static_cast<uint8_t>(sum + (old & 0xFF) + (old >> 8) - (mah & 0xFF) - (mah >> 8));
  // Reading MACDataSum() moves the X3's gauge to the next block: select this one again.
  if (!writeReg16(addr, BQ27220_MAC_CONTROL, address)) return false;
  delay(10);
  return writeReg16(addr, BQ27220_MAC_DATA, __builtin_bswap16(mah)) && writeReg16(addr, BQ27220_MAC_DATA_SUM, sum);
}

bool bq27220LoadStep(const uint8_t addr, const uint16_t mah, const unsigned long now) {
  Bq27220Load& s = bq27220Load;
  if (s.step == 9 || static_cast<long>(now - s.at) < 0) return s.step != 9;
  const auto next = [&](const uint8_t step, const unsigned long gap) {
    s.step = step;
    s.at = now + gap;
    return true;
  };
  // Leaves CONFIG UPDATE (with reinit once a block was written), then seals.
  const auto close = [&] {
    if (s.cfg) {
      s.cfg = false;
      s.since = now;
      if (writeReg16(addr, BQ27220_CONTROL, s.wrote ? 0x0091 : 0x0092)) return next(8, 500);
    }
    writeReg16(addr, BQ27220_CONTROL, 0x0030);
    s.step = 9;
    return false;
  };
  uint16_t status = 0;
  if (s.step == 0) {
    uint16_t dc = 0;
    uint16_t fcc = 0;
    s.step = 9;
    if (!readReg16(addr, BQ27220_DESIGN_CAPACITY, dc) || !readReg16(addr, BQ27220_OPERATION_STATUS, status)) {
      LOG_ERR("PWR", "BQ27220 capacity check failed");
      return false;
    }
    s.cfg = status & 0x0400;
    const uint8_t security = (status >> 1) & 3;
    if (dc == mah && (s.cfg || security != 3)) {
      s.wrote = true;  // an earlier load was cut short
      return close();
    }
    if (dc == mah ? !readReg16(addr, BQ27220_FULL_CHARGE_CAPACITY, fcc) || !bq27220TooHigh(fcc, mah) : dc != 3000) {
      return false;
    }
    s.step = security == 3 ? 1 : security == 1 ? 5 : 3;
  }
  if (s.step <= 5) {
    s.cfg |= s.step == 5;
    s.since = now;
    if (!writeReg16(addr, BQ27220_CONTROL, BQ27220_UNLOCK[s.step - 1])) return close();
    return next(s.step + 1, s.step == 5 ? 500 : 1500);
  }
  if (s.step == 6 || s.step == 8) {
    const bool reached = readReg16(addr, BQ27220_OPERATION_STATUS, status) && ((status & 0x0400) != 0) == (s.step == 6);
    if (!reached && now - s.since < 5000) return next(s.step, 500);
    // Learned FCC first, Design Capacity half a second later: the X3's gauge can miss
    // a block select sent right after a block write. If anything fails, Design
    // Capacity still reads 3000 or FullChargeCapacity() too high, and the next start retries.
    if (s.step == 6 && reached && bq27220WriteParam(addr, BQ27220_DM_LEARNED_FCC, mah)) return next(7, 500);
  } else {
    bq27220WriteParam(addr, BQ27220_DM_DESIGN_CAPACITY, mah);
  }
  return close();
}

}  // namespace

bool loadBq27220DesignCapacity(const BoardConfig::BatteryGaugeConfig& gauge, const uint16_t mah) {
  Bq27220Load& s = bq27220Load;
  // BatteryMonitor starts Wire on its first read, which may still be ahead of this one.
  if (s.step == 0) Wire.begin(gauge.i2cSda, gauge.i2cScl, gauge.i2cHz);

  if (bq27220LoadStep(gauge.gaugeAddr, mah, millis())) {
    s.reportDue = true;
    return true;
  }
  if (!s.reportDue) return false;
  s.reportDue = false;

  uint16_t designCapacity = 0;
  uint16_t fullChargeCapacity = 0;
  uint16_t status = 0;
  delay(10);  // the gauge has just been sent SEALED
  readReg16(gauge.gaugeAddr, BQ27220_DESIGN_CAPACITY, designCapacity);
  readReg16(gauge.gaugeAddr, BQ27220_FULL_CHARGE_CAPACITY, fullChargeCapacity);
  readReg16(gauge.gaugeAddr, BQ27220_OPERATION_STATUS, status);
  LOG_INF("PWR", "gauge design=%u fcc=%u status=0x%04X", designCapacity, fullChargeCapacity, status);
  return false;
}
