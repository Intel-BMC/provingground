/*
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <unistd.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "pfr.hpp"
#include "file.hpp"
#include "spiDev.hpp"

namespace intel
{
namespace pfr
{
// TODO: Dynamically pull these values from configuration
// entity-manager, when needed
static constexpr int i2cBusNumber = 4;
// below given is 7bit address. Its 8bit addr is 0x70
static constexpr int i2cSlaveAddress = 0x38;

// CPLD mailbox registers
static constexpr uint8_t cpldROTVersion = 0x01;
static constexpr uint8_t cpldROTSvn = 0x02;
static constexpr uint8_t platformState = 0x03;
static constexpr uint8_t recoveryCount = 0x04;
static constexpr uint8_t lastRecoveryReason = 0x05;
static constexpr uint8_t panicEventCount = 0x06;
static constexpr uint8_t panicEventReason = 0x07;
static constexpr uint8_t majorErrorCode = 0x08;
static constexpr uint8_t minorErrorCode = 0x09;
static constexpr uint8_t provisioningStatus = 0x0A;
static constexpr uint8_t bmcBootCheckpoint = 0x0F;
static constexpr uint8_t pchActiveMajorVersion = 0x15;
static constexpr uint8_t pchActiveMinorVersion = 0x16;
static constexpr uint8_t pchRecoveryMajorVersion = 0x1B;
static constexpr uint8_t pchRecoveryMinorVersion = 0x1C;

static constexpr uint8_t ufmLockedMask = (0x1 << 0x04);
static constexpr uint8_t ufmProvisionedMask = (0x1 << 0x05);

// PFR MTD devices
static constexpr const char* bmcActiveImgPfmMTDDev = "/dev/mtd/pfm";
static constexpr const char* bmcRecoveryImgMTDDev = "/dev/mtd/rc-image";

// PFM offset in full image
static constexpr const uint32_t pfmBaseOffsetInImage = 0x400;

// OFFSET values in PFM
static constexpr const uint32_t verOffsetInPFM = 0x406;
static constexpr const uint32_t buildNumOffsetInPFM = 0x40C;
static constexpr const uint32_t buildHashOffsetInPFM = 0x40D;

std::string toHexString(const uint8_t val)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(2) << std::hex
           << static_cast<int>(val);
    return stream.str();
}

static std::string readVersionFromCPLD(const uint8_t majorReg,
                                       const uint8_t minorReg)
{
    try
    {
        I2CFile cpldDev(i2cBusNumber, i2cSlaveAddress, O_RDWR | O_CLOEXEC);
        uint8_t majorVer = cpldDev.i2cReadByteData(majorReg);
        uint8_t minorVer = cpldDev.i2cReadByteData(minorReg);
        // Major and Minor versions should be binary encoded strings.
        std::string version =
            std::to_string(majorVer) + "." + std::to_string(minorVer);
        return version;
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Exception caught in readVersionFromCPLD.",
            phosphor::logging::entry("MSG=%s", e.what()));
        return "";
    }
}

static std::string readBMCVersionFromSPI(const ImageType& imgType)
{
    std::string mtdDev;
    uint32_t verOffset = verOffsetInPFM;
    uint32_t bldNumOffset = buildNumOffsetInPFM;
    uint32_t bldHashOffset = buildHashOffsetInPFM;

    if (imgType == ImageType::bmcActive)
    {
        // For Active image, PFM is emulated as separate MTD device.
        mtdDev = bmcActiveImgPfmMTDDev;
    }
    else if (imgType == ImageType::bmcRecovery)
    {
        // For Recovery image, PFM is part of compressed Image
        // at offset 0x400.
        mtdDev = bmcRecoveryImgMTDDev;
        verOffset += pfmBaseOffsetInImage;
        bldNumOffset += pfmBaseOffsetInImage;
        bldHashOffset += pfmBaseOffsetInImage;
    }
    else
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Invalid image type passed to readBMCVersionFromSPI.");
        return "";
    }

    uint8_t buildNo = 0;
    std::array<uint8_t, 2> ver;
    std::array<uint8_t, 3> buildHash;

    try
    {
        SPIDev spiDev(mtdDev);
        spiDev.spiReadData(verOffset, ver.size(),
                           reinterpret_cast<void*>(ver.data()));
        spiDev.spiReadData(bldNumOffset, sizeof(buildNo),
                           reinterpret_cast<void*>(&buildNo));
        spiDev.spiReadData(bldHashOffset, buildHash.size(),
                           reinterpret_cast<void*>(buildHash.data()));
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Exception caught in readBMCVersionFromSPI.",
            phosphor::logging::entry("MSG=%s", e.what()));
        return "";
    }

    // Version format: <major>.<minor>-<build bum>-g<build hash>
    // Example: 0.11-7-g1e5c2d
    // Major, minor and build numberare BCD encoded.
    std::string version =
        std::to_string(ver[0]) + "." + std::to_string(ver[1]) + "-" +
        std::to_string(buildNo) + "-g" + toHexString(buildHash[0]) +
        toHexString(buildHash[1]) + toHexString(buildHash[2]);
    return version;
}

std::string getFirmwareVersion(const ImageType& imgType)
{
    switch (imgType)
    {
        case (ImageType::cpldActive):
        case (ImageType::cpldRecovery):
        {
            // TO-DO: Need to update once CPLD supported Firmware is available
            return readVersionFromCPLD(cpldROTVersion, cpldROTSvn);
        }
        case (ImageType::biosActive):
        {
            return readVersionFromCPLD(pchActiveMajorVersion,
                                       pchActiveMinorVersion);
        }
        case (ImageType::biosRecovery):
        {
            return readVersionFromCPLD(pchRecoveryMajorVersion,
                                       pchRecoveryMinorVersion);
        }
        case (ImageType::bmcActive):
        case (ImageType::bmcRecovery):
        {
            return readBMCVersionFromSPI(imgType);
        }
        default:
            // Invalid image Type.
            return "";
    }
}

int getProvisioningStatus(bool& ufmLocked, bool& ufmProvisioned)
{
    try
    {
        I2CFile cpldDev(i2cBusNumber, i2cSlaveAddress, O_RDWR | O_CLOEXEC);
        uint8_t provStatus = cpldDev.i2cReadByteData(provisioningStatus);
        ufmLocked = (provStatus & ufmLockedMask);
        ufmProvisioned = (provStatus & ufmProvisionedMask);
        return 0;
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Exception caught in getProvisioningStatus.",
            phosphor::logging::entry("MSG=%s", e.what()));
        return -1;
    }
}

int readCpldReg(const ActionType& action, uint8_t& value)
{
    uint8_t cpldReg;

    switch (action)
    {
        case (ActionType::recoveryCount):
            cpldReg = recoveryCount;
            break;
        case (ActionType::recoveryReason):
            cpldReg = lastRecoveryReason;
            break;
        case (ActionType::panicCount):
            cpldReg = panicEventCount;
            break;
        case (ActionType::panicReason):
            cpldReg = panicEventReason;
            break;
        case (ActionType::majorError):
            cpldReg = majorErrorCode;
            break;
        case (ActionType::minorError):
            cpldReg = minorErrorCode;
            break;

        default:
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Invalid CPLD read action.");
            return -1;
    }

    try
    {
        I2CFile cpldDev(i2cBusNumber, i2cSlaveAddress, O_RDWR | O_CLOEXEC);
        value = cpldDev.i2cReadByteData(cpldReg);
        return 0;
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Exception caught in readCpldReg.",
            phosphor::logging::entry("MSG=%s", e.what()));
        return -1;
    }
}

int setBMCBootCheckpoint(const uint8_t checkPoint)
{
    try
    {
        I2CFile cpldDev(i2cBusNumber, i2cSlaveAddress, O_RDWR | O_CLOEXEC);
        cpldDev.i2cWriteByteData(bmcBootCheckpoint, checkPoint);
        phosphor::logging::log<phosphor::logging::level::INFO>(
            "Successfully set the PFR CPLD checkpoint 9.");
        return 0;
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Exception caught in setBMCBootCheckout.",
            phosphor::logging::entry("MSG=%s", e.what()));
        return -1;
    }
}

} // namespace pfr
} // namespace intel
