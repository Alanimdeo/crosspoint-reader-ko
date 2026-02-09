#include "CrossPointState.h"

#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Serialization.h>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 4;
constexpr char STATE_FILE[] = "/.crosspoint/state.bin";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  FsFile outputFile;
  if (!Storage.openFileForWrite("CPS", STATE_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, openEpubPath);
  serialization::writePod(outputFile, lastSleepImage);
  serialization::writePod(outputFile, readerActivityLoadCount);
  serialization::writePod(outputFile, lastSleepFromReader);
  outputFile.close();
  return true;
}

bool CrossPointState::loadFromFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE, inputFile)) {
    return false;
  }

  // Check file size for sanity
  const uint32_t fileSize = inputFile.size();
  if (fileSize < 1 || fileSize > 1024) {
    Serial.printf("[%lu] [STA] State file corrupted (size=%u), deleting\n", millis(), fileSize);
    inputFile.close();
    Storage.remove(STATE_FILE);
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version > STATE_FILE_VERSION) {
    Serial.printf("[%lu] [STA] Deserialization failed: Unknown version %u, deleting state file\n", millis(), version);
    inputFile.close();
    Storage.remove(STATE_FILE);
    return false;
  }

  serialization::readString(inputFile, openEpubPath);
  if (version >= 2) {
    serialization::readPod(inputFile, lastSleepImage);
  } else {
    lastSleepImage = 0;
  }

  if (version >= 3) {
    serialization::readPod(inputFile, readerActivityLoadCount);
  }

  if (version >= 4) {
    serialization::readPod(inputFile, lastSleepFromReader);
  } else {
    lastSleepFromReader = false;
  }

  inputFile.close();
  return true;
}
