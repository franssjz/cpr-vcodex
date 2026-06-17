#include "HalTiltSensor.h"

#include <Logging.h>

#include <cmath>

HalTiltSensor halTiltSensor;

bool HalTiltSensor::writeReg(uint8_t reg, uint8_t val) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool HalTiltSensor::readReg(uint8_t reg, uint8_t* val) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(_i2cAddr, static_cast<uint8_t>(1));
  if (Wire.available() < 1) {
    return false;
  }
  *val = Wire.read();
  return true;
}

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(REG_GX_L);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(_i2cAddr, static_cast<uint8_t>(6));
  if (Wire.available() < 6) {
    return false;
  }

  auto readInt16 = []() -> int16_t {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    return static_cast<int16_t>((hi << 8) | lo);
  };

  constexpr float SCALE = 1.0f / 64.0f;  // +/-512 dps full-scale range
  gx = readInt16() * SCALE;
  gy = readInt16() * SCALE;
  gz = readInt16() * SCALE;
  return true;
}

void HalTiltSensor::begin() {
#ifdef FORCE_TILT_SENSOR_AVAILABLE
  _available = true;
  _isAwake = false;
  _initMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "Tilt sensor override active via build flag");
  return;
#endif

  if (!gpio.deviceIsX3()) {
    _available = false;
    return;
  }

  uint8_t whoami = 0;
  _i2cAddr = I2C_ADDR_QMI8658;
  if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
    _i2cAddr = I2C_ADDR_QMI8658_ALT;
    if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
      LOG_ERR("GYR", "QMI8658 IMU not found");
      _available = false;
      return;
    }
  }

  LOG_INF("GYR", "QMI8658 IMU found at 0x%02X", _i2cAddr);

  if (!writeReg(REG_CTRL7, CTRL7_DISABLE_ALL) || !writeReg(REG_CTRL3, CTRL3_FS_512DPS | CTRL3_ODR_28HZ) ||
      !writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    LOG_ERR("GYR", "QMI8658 register configuration failed");
    _available = false;
    return;
  }

  _available = true;
  _initMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "QMI8658 gyro initialized and put to sleep");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

#ifdef FORCE_TILT_SENSOR_AVAILABLE
  _lastPollMs = millis();
  _lastTiltMs = millis();
  _wakeMs = millis();
  return true;
#endif

  if ((millis() - _initMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (writeReg(REG_CTRL1, CTRL1_BASE) && writeReg(REG_CTRL7, CTRL7_GYRO_ENABLE)) {
    _lastPollMs = millis();
    _lastTiltMs = millis();
    _wakeMs = millis();
    LOG_INF("GYR", "QMI8658 woke up");
    return true;
  }

  LOG_ERR("GYR", "Failed to wake QMI8658");
  return false;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

#ifdef FORCE_TILT_SENSOR_AVAILABLE
  clearPendingEvents();
  _inTilt = false;
  _isAwake = false;
  return true;
#endif

  if ((millis() - _wakeMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (writeReg(REG_CTRL7, CTRL7_DISABLE_ALL) && writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    clearPendingEvents();
    _inTilt = false;
    _isAwake = false;
    LOG_INF("GYR", "QMI8658 entered sleep mode");
    return true;
  }

  LOG_ERR("GYR", "Failed to put QMI8658 to sleep");
  return false;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  if (!_available) {
    return;
  }

  if (!inReader) {
    if (_isAwake) {
      _isAwake = !deepSleep();
    }
    return;
  }

  if ((mode != CrossPointTiltPageTurn::TILT_OFF) && !_isAwake) {
    _isAwake = wake();
    return;
  }
  if ((mode == CrossPointTiltPageTurn::TILT_OFF) && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  if (mode == CrossPointTiltPageTurn::TILT_OFF) {
    return;
  }

  const unsigned long now = millis();
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }
  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  float tiltAxis = gx;
  switch (orientation) {
    case CrossPointTiltOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointTiltOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    case CrossPointTiltOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointTiltOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    default:
      break;
  }

  if (_inTilt) {
    if (std::fabs(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
    return;
  }

  if ((now - _lastTiltMs) < COOLDOWN_MS) {
    return;
  }

  if (tiltAxis > RATE_THRESHOLD_DPS) {
    _tiltForwardEvent = true;
    _hadActivity = true;
    _inTilt = true;
    _lastTiltMs = now;
    LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
  } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
    _tiltBackEvent = true;
    _hadActivity = true;
    _inTilt = true;
    _lastTiltMs = now;
    LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool value = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return value;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool value = _tiltBackEvent;
  _tiltBackEvent = false;
  return value;
}

bool HalTiltSensor::hadActivity() {
  const bool value = _hadActivity;
  _hadActivity = false;
  return value;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
}
