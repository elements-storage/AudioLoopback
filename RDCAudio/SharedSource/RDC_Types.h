// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.
//
// Background Music is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Background Music. If not, see <http://www.gnu.org/licenses/>.

//
//  RDC_Types.h
//  SharedSource
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//

#ifndef SharedSource__RDC_Types
#define SharedSource__RDC_Types

// STL Includes
#if defined(__cplusplus)
#include <stdexcept>
#endif

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


#pragma mark Project URLs

#pragma mark IDs

#define kRDCDeviceUID                "RDCDevice"
#define kRDCDeviceModelUID           "RDCDeviceModelUID"
#define kRDCNullDeviceUID            "RDCNullDevice"
#define kRDCNullDeviceModelUID       "RDCNullDeviceModelUID"

// The object IDs for the audio objects this driver implements.
//
// RDCDevice always publishes this fixed set of objects (except when RDCDevice's volume or mute
// controls are disabled). We might need to change that at some point, but so far it hasn't caused
// any problems and it makes the driver much simpler.
enum
{
	kObjectID_PlugIn                            = kAudioObjectPlugInObject,
    // RDCDevice
	kObjectID_Device                            = 2,   // Belongs to kObjectID_PlugIn
	kObjectID_Stream_Input                      = 3,   // Belongs to kObjectID_Device
	kObjectID_Stream_Output                     = 4,   // Belongs to kObjectID_Device
	kObjectID_Volume_Output_Master              = 5,   // Belongs to kObjectID_Device
	kObjectID_Mute_Output_Master                = 6,   // Belongs to kObjectID_Device
    // Null Device
    kObjectID_Device_Null                       = 7,   // Belongs to kObjectID_PlugIn
    kObjectID_Stream_Null                       = 8,   // Belongs to kObjectID_Device_Null
};

// AudioObjectPropertyElement docs: "Elements are numbered sequentially where 0 represents the
// master element."
static const AudioObjectPropertyElement kMasterChannel = kAudioObjectPropertyElementMaster;

#pragma RDC Plug-in Custom Properties

enum
{
    // A CFBoolean. True if the null device is enabled. Settable, false by default.
    kAudioPlugInCustomPropertyNullDeviceActive = 'nuld'
};

#pragma mark RDCDevice Custom Properties

enum
{
    // A CFArray of CFBooleans indicating which of RDCDevice's controls are enabled. All controls are enabled
    // by default. This property is settable. See the array indices below for more info.
    kAudioDeviceCustomPropertyEnabledOutputControls                   = 'bgct'
};


// kAudioDeviceCustomPropertyEnabledOutputControls indices
enum
{
    // True if RDCDevice's master output volume control is enabled.
    kRDCEnabledOutputControlsIndex_Volume = 0,
    // True if RDCDevice's master output mute control is enabled.
    kRDCEnabledOutputControlsIndex_Mute   = 1
};

#pragma mark RDCDevice Custom Property Addresses

static const AudioObjectPropertyAddress kRDCEnabledOutputControlsAddress = {
    kAudioDeviceCustomPropertyEnabledOutputControls,
    kAudioObjectPropertyScopeOutput,
    kAudioObjectPropertyElementMaster
};


#pragma mark Exceptions

#if defined(__cplusplus)

class RDC_InvalidClientException : public std::runtime_error {
public:
    RDC_InvalidClientException() : std::runtime_error("InvalidClient") { }
};

class RDC_InvalidClientPIDException : public std::runtime_error {
public:
    RDC_InvalidClientPIDException() : std::runtime_error("InvalidClientPID") { }
};

class RDC_InvalidClientRelativeVolumeException : public std::runtime_error {
public:
    RDC_InvalidClientRelativeVolumeException() : std::runtime_error("InvalidClientRelativeVolume") { }
};

class RDC_InvalidClientPanPositionException : public std::runtime_error {
public:
    RDC_InvalidClientPanPositionException() : std::runtime_error("InvalidClientPanPosition") { }
};

class RDC_DeviceNotSetException : public std::runtime_error {
public:
    RDC_DeviceNotSetException() : std::runtime_error("DeviceNotSet") { }
};

#endif

// Assume we've failed to start the output device if it isn't running IO after this timeout expires.
//
// Currently set to 30s because some devices, e.g. AirPlay, can legitimately take that long to start.
//
// TODO: Should we have a timeout at all? Is there a notification we can subscribe to that will tell us whether the
//       device is still making progress? Should we regularly poll mOutputDevice.IsAlive() while we're waiting to
//       check it's still responsive?
static const UInt64 kStartIOTimeoutNsec = 30 * NSEC_PER_SEC;

#endif /* SharedSource__RDC_Types */

