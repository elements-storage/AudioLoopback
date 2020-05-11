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
//  RDC_Device.cpp
//  RDCDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//  Copyright © 2019 Gordon Childs
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_Device.cpp from Apple's SimpleAudioDriver Plug-In sample code. Also uses a few sections from Apple's
//  NullAudio.c sample code (found in the same sample project).
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

// Self Include
#include "RDC_Device.h"

// Local Includes
#include "RDC_PlugIn.h"
#include "RDC_Utils.h"

// PublicUtility Includes
#include "CADispatchQueue.h"
#include "CACFArray.h"
#include "CADebugMacros.h"
#include "CAHostTimeBase.h"

// STL Includes
#include <stdexcept>

// System Includes
#include <CoreAudio/AudioHardwareBase.h>


#pragma mark Construction/Destruction

pthread_once_t				RDC_Device::sStaticInitializer = PTHREAD_ONCE_INIT;
RDC_Device*					RDC_Device::sInstance = nullptr;

RDC_Device&	RDC_Device::GetInstance()
{
    pthread_once(&sStaticInitializer, StaticInitializer);
    return *sInstance;
}

void	RDC_Device::StaticInitializer()
{
    try
    {
        // The main instance, usually referred to in the code as "RDCDevice". This is the device
        // that appears in System Preferences as "Background Music".
        sInstance = new RDC_Device(kObjectID_Device,
                                   CFSTR(kDeviceName),
								   CFSTR(kRDCDeviceUID),
								   CFSTR(kRDCDeviceModelUID),
                                   kObjectID_Stream_Input,
                                   kObjectID_Stream_Output,
								   kObjectID_Volume_Output_Master,
								   kObjectID_Mute_Output_Master);
        
        // Set up the UI sounds device's volume control.
        RDC_VolumeControl& volumeControl = sInstance->mVolumeControl;
        // Default to full volume.
        volumeControl.SetVolumeScalar(1.0f);
        // Make the volume curve a bit steeper than the default.
        volumeControl.GetVolumeCurve().SetTransferFunction(CAVolumeCurve::kPow2Over1Curve);
        volumeControl.SetWillApplyVolumeToAudio(true);

        sInstance->Activate();
    }
    catch(...)
    {
        DebugMsg("RDC_Device::StaticInitializer: failed to create the devices");

        delete sInstance;
        sInstance = nullptr;
    }
}

RDC_Device::RDC_Device(AudioObjectID inObjectID,
					   const CFStringRef __nonnull inDeviceName,
					   const CFStringRef __nonnull inDeviceUID,
					   const CFStringRef __nonnull inDeviceModelUID,
                       AudioObjectID inInputStreamID,
                       AudioObjectID inOutputStreamID,
					   AudioObjectID inOutputVolumeControlID,
					   AudioObjectID inOutputMuteControlID)
:
	RDC_AbstractDevice(inObjectID, kAudioObjectPlugInObject),
	mStateMutex("Device State"),
	mIOMutex("Device IO"),
	mDeviceName(inDeviceName),
	mDeviceUID(inDeviceUID),
	mDeviceModelUID(inDeviceModelUID),
    mWrappedAudioEngine(nullptr),
    mClients(inObjectID, &mTaskQueue),
    mInputStream(inInputStreamID, inObjectID, false, kSampleRateDefault),
    mOutputStream(inOutputStreamID, inObjectID, false, kSampleRateDefault),
    mVolumeControl(inOutputVolumeControlID, GetObjectID()),
    mMuteControl(inOutputMuteControlID, GetObjectID())
{
    // Initialises the loopback clock with the default sample rate and, if there is one, sets the wrapped device to the same sample rate
    SetSampleRate(kSampleRateDefault, true);
}

RDC_Device::~RDC_Device()
{
}

void	RDC_Device::Activate()
{
	CAMutex::Locker theStateLocker(mStateMutex);

	//	Open the connection to the driver and initialize things.
	//_HW_Open();

	mInputStream.Activate();
	mOutputStream.Activate();

	if(mVolumeControl.GetObjectID() != kAudioObjectUnknown)
	{
		mVolumeControl.Activate();
	}

    if(mMuteControl.GetObjectID() != kAudioObjectUnknown)
	{
		mMuteControl.Activate();
	}
	
	//	Call the super-class, which just marks the object as active
	RDC_AbstractDevice::Activate();
}

void	RDC_Device::Deactivate()
{
	//	When this method is called, the object is basically dead, but we still need to be thread
	//	safe. In this case, we also need to be safe vs. any IO threads, so we need to take both
	//	locks.
	CAMutex::Locker theStateLocker(mStateMutex);
	CAMutex::Locker theIOLocker(mIOMutex);

    // Mark the device's sub-objects inactive.
	mInputStream.Deactivate();
	mOutputStream.Deactivate();
    mVolumeControl.Deactivate();
    mMuteControl.Deactivate();

	//	mark the object inactive by calling the super-class
	RDC_AbstractDevice::Deactivate();
	
	//	close the connection to the driver
	//_HW_Close();
}

void    RDC_Device::InitLoopback()
{
    // Calculate the number of host clock ticks per frame for our loopback clock.
    mLoopbackTime.hostTicksPerFrame = CAHostTimeBase::GetFrequency() / mLoopbackSampleRate;
    
    //  Allocate (or re-allocate) the loopback buffer.
    //  2 channels * 32-bit float = bytes in each frame
    //  Pass 1 for nChannels because it's going to be storing interleaved audio, which means we
    //  don't need a separate buffer for each channel.
	mLoopbackRingBuffer.Allocate(1, 2 * sizeof(Float32), kLoopbackRingBufferFrameSize);
}

#pragma mark Property Operations

bool	RDC_Device::HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	This object owns several API-level objects. So the first thing to do is to figure out
	//	which object this request is really for. Note that mObjectID is an invariant as this
	//	driver's structure does not change dynamically. It will always have the parts it has.
	bool theAnswer = false;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_HasProperty(inObjectID, inClientPID, inAddress);
	}
    else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).HasProperty(inObjectID, inClientPID, inAddress);
	}

	return theAnswer;
}

bool	RDC_Device::IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	bool theAnswer = false;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_IsPropertySettable(inObjectID, inClientPID, inAddress);
	}
	else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).IsPropertySettable(inObjectID, inClientPID, inAddress);
	}

	return theAnswer;
}

UInt32	RDC_Device::GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	UInt32 theAnswer = 0;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}
	else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}

	return theAnswer;
}

void	RDC_Device::GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
    ThrowIfNULL(outData, std::runtime_error("!outData"), "RDC_Device::GetPropertyData: !outData");
    
	if(inObjectID == mObjectID)
	{
		Device_GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
	else
	{
		GetOwnedObjectByID(inObjectID).GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
}

void	RDC_Device::SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
    ThrowIfNULL(inData, std::runtime_error("no data"), "RDC_Device::SetPropertyData: no data");
    
	if(inObjectID == mObjectID)
	{
		Device_SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
	}
	else
    {
        GetOwnedObjectByID(inObjectID).SetPropertyData(inObjectID,
                                                       inClientPID,
                                                       inAddress,
                                                       inQualifierDataSize,
                                                       inQualifierData,
                                                       inDataSize,
                                                       inData);
		if(IsStreamID(inObjectID))
		{
            // When one of the stream's sample rate changes, set the new sample rate for both
            // streams and the device. The streams check the new format before this point but don't
            // change until the device tells them to, as it has to get the host to pause IO first.
            if(inAddress.mSelector == kAudioStreamPropertyVirtualFormat ||
               inAddress.mSelector == kAudioStreamPropertyPhysicalFormat)
            {
                const AudioStreamBasicDescription* theNewFormat =
                    reinterpret_cast<const AudioStreamBasicDescription*>(inData);
                RequestSampleRate(theNewFormat->mSampleRate);
            }
		}
	}
}

#pragma mark Device Property Operations

bool	RDC_Device::Device_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
        case kAudioDevicePropertyStreams:
        case kAudioDevicePropertyIcon:
        case kAudioObjectPropertyCustomPropertyInfoList:
        case kAudioDeviceCustomPropertyEnabledOutputControls:
			theAnswer = true;
			break;
			
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			theAnswer = (inAddress.mScope == kAudioObjectPropertyScopeInput) || (inAddress.mScope == kAudioObjectPropertyScopeOutput);
			break;
			
		default:
			theAnswer = RDC_AbstractDevice::HasProperty(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

bool	RDC_Device::Device_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
    {
		case kAudioDevicePropertyStreams:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyIcon:
        case kAudioObjectPropertyCustomPropertyInfoList:
			break;
            
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDeviceCustomPropertyEnabledOutputControls:
			theAnswer = true;
			break;
		
		default:
			theAnswer = RDC_AbstractDevice::IsPropertySettable(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

UInt32	RDC_Device::Device_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	UInt32 theAnswer = 0;

	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyOwnedObjects:
            {
                switch(inAddress.mScope)
                {
                    case kAudioObjectPropertyScopeGlobal:
                        theAnswer = GetNumberOfSubObjects() * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeInput:
                        theAnswer = kNumberOfInputSubObjects * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeOutput:
                        theAnswer = kNumberOfOutputStreams * sizeof(AudioObjectID);
                        theAnswer += GetNumberOfOutputControls() * sizeof(AudioObjectID);
                        break;

					default:
						break;
                };
            }
			break;

        case kAudioDevicePropertyStreams:
            {
                switch(inAddress.mScope)
                {
                    case kAudioObjectPropertyScopeGlobal:
                        theAnswer = kNumberOfStreams * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeInput:
                        theAnswer = kNumberOfInputStreams * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeOutput:
                        theAnswer = kNumberOfOutputStreams * sizeof(AudioObjectID);
                        break;

					default:
						break;
                };
            }
			break;

        case kAudioObjectPropertyControlList:
            theAnswer = GetNumberOfOutputControls() * sizeof(AudioObjectID);
            break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			theAnswer = 1 * sizeof(AudioValueRange);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			theAnswer = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			theAnswer = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
			break;

        case kAudioDevicePropertyIcon:
            theAnswer = sizeof(CFURLRef);
            break;
            
        case kAudioObjectPropertyCustomPropertyInfoList:
            theAnswer = sizeof(AudioServerPlugInCustomPropertyInfo) * 6;
            break;
            
        case kAudioDeviceCustomPropertyEnabledOutputControls:
            theAnswer = sizeof(CFArrayRef);
            break;
		
		default:
			theAnswer = RDC_AbstractDevice::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
			break;
	};

	return theAnswer;
}

void	RDC_Device::Device_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required.
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.

	UInt32 theNumberItemsToFetch;
	UInt32 theItemIndex;

	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
			//	This is the human readable name of the device. Note that in this case we return a
			//	value that is a key into the localizable strings in this bundle. This allows us to
			//	return a localized name for the device.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyName for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceName;
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in. Note that in this case
			//	we return a value that is a key into the localizable strings in this bundle. This
			//	allows us to return a localized name for the manufacturer.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR(kDeviceManufacturerName);
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	The device owns its streams and controls. Note that what is returned here
			//	depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all objects
                    {
                        CAMutex::Locker theStateLocker(mStateMutex);

                        if(theNumberItemsToFetch > GetNumberOfSubObjects())
                        {
                            theNumberItemsToFetch = GetNumberOfSubObjects();
                        }

                        //	fill out the list with as many objects as requested, which is everything
                        if(theNumberItemsToFetch > 0)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
                        }

                        if(theNumberItemsToFetch > 1)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStream.GetObjectID();
                        }

                        // If at least one of the controls is enabled, and there's room, return one.
						if(theNumberItemsToFetch > 2)
						{
							if(mVolumeControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[2] = mVolumeControl.GetObjectID();
							}
							else if(mMuteControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[2] = mMuteControl.GetObjectID();
							}
						}

						// If both controls are enabled, and there's room, return the mute control as well.
                        if(theNumberItemsToFetch > 3 && mVolumeControl.IsActive() && mMuteControl.IsActive())
                        {
							reinterpret_cast<AudioObjectID*>(outData)[3] = mMuteControl.GetObjectID();
                        }
                    }
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputSubObjects)
					{
						theNumberItemsToFetch = kNumberOfInputSubObjects;
					}
					
					//	fill out the list with the right objects
					if(theNumberItemsToFetch > 0)
					{
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
                    {
                        CAMutex::Locker theStateLocker(mStateMutex);

                        if(theNumberItemsToFetch > GetNumberOfOutputControls())
                        {
                            theNumberItemsToFetch = GetNumberOfOutputControls();
                        }

                        //	fill out the list with the right objects
                        if(theNumberItemsToFetch > 0)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStream.GetObjectID();
                        }

						// If at least one of the controls is enabled, and there's room, return one.
						if(theNumberItemsToFetch > 1)
						{
							if(mVolumeControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[1] = mVolumeControl.GetObjectID();
							}
							else if(mMuteControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[1] = mMuteControl.GetObjectID();
							}
						}

						// If both controls are enabled, and there's room, return the mute control as well.
						if(theNumberItemsToFetch > 2 && mVolumeControl.IsActive() && mMuteControl.IsActive())
						{
							reinterpret_cast<AudioObjectID*>(outData)[2] = mMuteControl.GetObjectID();
						}
                    }
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			//	This is a CFString that is a persistent token that can identify the same
			//	audio device across boot sessions. Note that two instances of the same
			//	device must have different values for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceUID;
			outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			//	This is a CFString that is a persistent token that can identify audio
			//	devices that are the same kind of device. Note that two instances of the
			//	save device must have the same value for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyModelUID for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceModelUID;
			outDataSize = sizeof(CFStringRef);
			break;
            
		case kAudioDevicePropertyDeviceIsRunning:
			//	This property returns whether or not IO is running for the device.
            ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsRunning for the device");
            *reinterpret_cast<UInt32*>(outData) = mClients.ClientsRunningIO() ? 1 : 0;
            outDataSize = sizeof(UInt32);
			break;

        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            ThrowIf(inDataSize < sizeof(UInt32),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "RDC_Device::GetPropertyData: not enough space for the return value of "
                    "kAudioDevicePropertyDeviceCanBeDefaultDevice for the device");
            *reinterpret_cast<UInt32*>(outData) = 1;
            outDataSize = sizeof(UInt32);
            break;

		case kAudioDevicePropertyStreams:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Note that what is returned here depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all streams
					if(theNumberItemsToFetch > kNumberOfStreams)
					{
						theNumberItemsToFetch = kNumberOfStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputStreams)
					{
						theNumberItemsToFetch = kNumberOfInputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > kNumberOfOutputStreams)
					{
						theNumberItemsToFetch = kNumberOfOutputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStream.GetObjectID();
					}
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
            {
                //	Calculate the number of items that have been requested. Note that this
                //	number is allowed to be smaller than the actual size of the list, in which
                //	case only that many items will be returned.
                theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
                if(theNumberItemsToFetch > 2)
                {
                    theNumberItemsToFetch = 2;
                }

                UInt32 theNumberOfItemsFetched = 0;

                CAMutex::Locker theStateLocker(mStateMutex);
                
                //	fill out the list with as many objects as requested
                if(theNumberItemsToFetch > 0)
                {
					if(mVolumeControl.IsActive())
                    {
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mVolumeControl.GetObjectID();
                        theNumberOfItemsFetched++;
                    }
                    else if(mMuteControl.IsActive())
                    {
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mMuteControl.GetObjectID();
                        theNumberOfItemsFetched++;
                    }
                }

                if(theNumberItemsToFetch > 1 && mVolumeControl.IsActive() && mMuteControl.IsActive())
                {
                    reinterpret_cast<AudioObjectID*>(outData)[1] = mMuteControl.GetObjectID();
                    theNumberOfItemsFetched++;
                }
                
                //	report how much we wrote
                outDataSize = theNumberOfItemsFetched * sizeof(AudioObjectID);
            }
			break;

        // TODO: Should we return the real kAudioDevicePropertyLatency and/or
        //       kAudioDevicePropertySafetyOffset for the real/wrapped output device?
        //       If so, should we also add on the extra latency added by Background Music? 

		case kAudioDevicePropertyNominalSampleRate:
			//	This property returns the nominal sample rate of the device.
            ThrowIf(inDataSize < sizeof(Float64),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyNominalSampleRate for the device");

            *reinterpret_cast<Float64*>(outData) = GetSampleRate();
            outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			//	This returns all nominal sample rates the device supports as an array of
			//	AudioValueRangeStructs. Note that for discrete sampler rates, the range
			//	will have the minimum value equal to the maximum value.
            //
            //  RDCDevice supports any sample rate so it can be set to match the output
            //  device when in loopback mode.
			
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioValueRange);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
                // 0 would cause divide-by-zero errors in other RDC_Device functions (and
                // wouldn't make sense anyway).
                ((AudioValueRange*)outData)[0].mMinimum = 1.0;
                // Just in case DBL_MAX would cause problems in a client for some reason,
                // use an arbitrary very large number instead. (It wouldn't make sense to
                // actually set the sample rate this high, but I don't know what a
                // reasonable maximum would be.)
                ((AudioValueRange*)outData)[0].mMaximum = 1000000000.0;
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			//	This property returns which two channels to use as left/right for stereo
			//	data by default. Note that the channel numbers are 1-based.
			ThrowIf(inDataSize < (2 * sizeof(UInt32)), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelsForStereo for the device");
			((UInt32*)outData)[0] = 1;
			((UInt32*)outData)[1] = 2;
			outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			//	This property returns the default AudioChannelLayout to use for the device
			//	by default. For this device, we return a stereo ACL.
			{
				UInt32 theACLSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
				ThrowIf(inDataSize < theACLSize, CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelLayout for the device");
				((AudioChannelLayout*)outData)->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
				((AudioChannelLayout*)outData)->mChannelBitmap = 0;
				((AudioChannelLayout*)outData)->mNumberChannelDescriptions = 2;
				for(theItemIndex = 0; theItemIndex < 2; ++theItemIndex)
				{
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelLabel = kAudioChannelLabel_Left + theItemIndex;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelFlags = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[0] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[1] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[2] = 0;
				}
				outDataSize = theACLSize;
			}
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			//	This property returns how many frames the HAL should expect to see between
			//	successive sample times in the zero time stamps this device provides.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyZeroTimeStampPeriod for the device");
			*reinterpret_cast<UInt32*>(outData) = kLoopbackRingBufferFrameSize;
			outDataSize = sizeof(UInt32);
            break;
            
        case kAudioDevicePropertyIcon:
            {
                // This property is a CFURL that points to the device's icon in the plugin's resource bundle
                ThrowIf(inDataSize < sizeof(CFURLRef), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyIcon for the device");
                
                CFBundleRef theBundle = CFBundleGetBundleWithIdentifier(RDC_PlugIn::GetInstance().GetBundleID());
                ThrowIf(theBundle == NULL, CAException(kAudioHardwareUnspecifiedError), "RDC_Device::Device_GetPropertyData: could not get the plugin bundle for kAudioDevicePropertyIcon");
                
                CFURLRef theURL = CFBundleCopyResourceURL(theBundle, CFSTR("DeviceIcon.icns"), NULL, NULL);
                ThrowIf(theURL == NULL, CAException(kAudioHardwareUnspecifiedError), "RDC_Device::Device_GetPropertyData: could not get the URL for kAudioDevicePropertyIcon");
                
                *reinterpret_cast<CFURLRef*>(outData) = theURL;
                outDataSize = sizeof(CFURLRef);
            }
            break;
            
        case kAudioObjectPropertyCustomPropertyInfoList:
            theNumberItemsToFetch = inDataSize / sizeof(AudioServerPlugInCustomPropertyInfo);
            
            //	clamp it to the number of items we have
            if(theNumberItemsToFetch > 1)
            {
                theNumberItemsToFetch = 1;
            }
            
            if(theNumberItemsToFetch > 0)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mSelector = kAudioDeviceCustomPropertyEnabledOutputControls;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }

            outDataSize = theNumberItemsToFetch * sizeof(AudioServerPlugInCustomPropertyInfo);
            break;            
            
        case kAudioDeviceCustomPropertyEnabledOutputControls:
            {
                ThrowIf(inDataSize < sizeof(CFArrayRef), CAException(kAudioHardwareBadPropertySizeError), "RDC_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyEnabledOutputControls for the device");
                CACFArray theEnabledControls(2, true);

				{
					CAMutex::Locker theStateLocker(mStateMutex);
					theEnabledControls.AppendCFType(mVolumeControl.IsActive() ? kCFBooleanTrue : kCFBooleanFalse);
					theEnabledControls.AppendCFType(mMuteControl.IsActive() ? kCFBooleanTrue : kCFBooleanFalse);
				}

                *reinterpret_cast<CFArrayRef*>(outData) = theEnabledControls.CopyCFArray();
                outDataSize = sizeof(CFArrayRef);
            }
            break;

		default:
			RDC_AbstractDevice::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	RDC_Device::Device_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	switch(inAddress.mSelector)
	{
        case kAudioDevicePropertyNominalSampleRate:
            ThrowIf(inDataSize < sizeof(Float64),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "RDC_Device::Device_SetPropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
            RequestSampleRate(*reinterpret_cast<const Float64*>(inData));
            break;

        case kAudioDeviceCustomPropertyEnabledOutputControls:
            {
                ThrowIf(inDataSize < sizeof(CFArrayRef),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "RDC_Device::Device_SetPropertyData: wrong size for the data for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                CFArrayRef theEnabledControlsRef = *reinterpret_cast<const CFArrayRef*>(inData);

                ThrowIfNULL(theEnabledControlsRef,
                            CAException(kAudioHardwareIllegalOperationError),
                            "RDC_Device::Device_SetPropertyData: null reference given for "
                            "kAudioDeviceCustomPropertyEnabledOutputControls");
                ThrowIf(CFGetTypeID(theEnabledControlsRef) != CFArrayGetTypeID(),
                        CAException(kAudioHardwareIllegalOperationError),
                        "RDC_Device::Device_SetPropertyData: CFType given for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls was not a CFArray");

                CACFArray theEnabledControls(theEnabledControlsRef, false);

                ThrowIf(theEnabledControls.GetNumberItems() != 2,
                        CAException(kAudioHardwareIllegalOperationError),
                        "RDC_Device::Device_SetPropertyData: Expected the CFArray given for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls to have exactly 2 elements");

                bool theVolumeControlEnabled;
                bool didGetBool = theEnabledControls.GetBool(kRDCEnabledOutputControlsIndex_Volume,
															 theVolumeControlEnabled);
                ThrowIf(!didGetBool,
                        CAException(kAudioHardwareIllegalOperationError),
                        "RDC_Device::Device_SetPropertyData: Expected CFBoolean for volume elem of "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                bool theMuteControlEnabled;
                didGetBool = theEnabledControls.GetBool(kRDCEnabledOutputControlsIndex_Mute,
														theMuteControlEnabled);
                ThrowIf(!didGetBool,
                        CAException(kAudioHardwareIllegalOperationError),
                        "RDC_Device::Device_SetPropertyData: Expected CFBoolean for mute elem of "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                RequestEnabledControls(theVolumeControlEnabled, theMuteControlEnabled);
            }
            break;

		default:
			RDC_AbstractDevice::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
    };
}

#pragma mark IO Operations

void	RDC_Device::StartIO(UInt32 inClientID)
{
    CAMutex::Locker theStateLocker(mStateMutex);
    
    // An overview of the process this function is part of:
    //   - A client starts IO.
    //   - The plugin host (the HAL) calls the StartIO function in RDC_PlugInInterface, which calls this function.
    //   - RDCDriver sends a message to RDCApp telling it to start the (real) audio hardware.
    //   - RDCApp starts the hardware and, after the hardware is ready, replies to RDCDriver's message.
    //   - RDCDriver lets the host know that it's ready to do IO by returning from StartIO.
    
    // Update our client data.
    //
    // We add the work to the task queue, rather than doing it here, because BeginIOOperation and EndIOOperation
    // also add this task to the queue and the updates should be done in order.
    bool didStartIO = mTaskQueue.QueueSync_StartClientIO(&mClients, inClientID);
    
    // We only tell the hardware to start if this is the first time IO has been started.
    if(didStartIO)
    {
        kern_return_t theError = _HW_StartIO();
        ThrowIfKernelError(theError,
                           CAException(theError),
                           "RDC_Device::StartIO: Failed to start because of an error calling down to the driver.");
    }
}

void	RDC_Device::StopIO(UInt32 inClientID)
{
    CAMutex::Locker theStateLocker(mStateMutex);
    
    // Update our client data.
    //
    // We add the work to the task queue, rather than doing it here, because BeginIOOperation and EndIOOperation also
    // add this task to the queue and the updates should be done in order.
    bool didStopIO = mTaskQueue.QueueSync_StopClientIO(&mClients, inClientID);
	
	//	we tell the hardware to stop if this is the last stop call
	if(didStopIO)
	{
		_HW_StopIO();
	}
}

void	RDC_Device::GetZeroTimeStamp(Float64& outSampleTime, UInt64& outHostTime, UInt64& outSeed)
{
	// accessing the buffers requires holding the IO mutex
	CAMutex::Locker theIOLocker(mIOMutex);
    
    if(mWrappedAudioEngine != NULL)
    {
    }
    else
    {
        // Without a wrapped device, we base our timing on the host. This is mostly from Apple's NullAudio.c sample code
    	UInt64 theCurrentHostTime;
    	Float64 theHostTicksPerRingBuffer;
    	Float64 theHostTickOffset;
    	UInt64 theNextHostTime;
    	
    	//	get the current host time
        theCurrentHostTime = CAHostTimeBase::GetTheCurrentTime();
    	
    	//	calculate the next host time
    	theHostTicksPerRingBuffer = mLoopbackTime.hostTicksPerFrame * kLoopbackRingBufferFrameSize;
    	theHostTickOffset = static_cast<Float64>(mLoopbackTime.numberTimeStamps + 1) * theHostTicksPerRingBuffer;
    	theNextHostTime = mLoopbackTime.anchorHostTime + static_cast<UInt64>(theHostTickOffset);
    	
    	//	go to the next time if the next host time is less than the current time
    	if(theNextHostTime <= theCurrentHostTime)
    	{
            mLoopbackTime.numberTimeStamps++;
    	}
    	
    	//	set the return values
    	outSampleTime = mLoopbackTime.numberTimeStamps * kLoopbackRingBufferFrameSize;
    	outHostTime = static_cast<UInt64>(mLoopbackTime.anchorHostTime + (static_cast<Float64>(mLoopbackTime.numberTimeStamps) * theHostTicksPerRingBuffer));
        // TODO: I think we should increment outSeed whenever this device switches to/from having a wrapped engine
    	outSeed = 1;
    }
}

void	RDC_Device::WillDoIOOperation(UInt32 inOperationID, bool& outWillDo, bool& outWillDoInPlace) const
{
	switch(inOperationID)
	{
        case kAudioServerPlugInIOOperationThread:
        case kAudioServerPlugInIOOperationReadInput:
        case kAudioServerPlugInIOOperationProcessOutput:
		case kAudioServerPlugInIOOperationWriteMix:
			outWillDo = true;
			outWillDoInPlace = true;
			break;

        case kAudioServerPlugInIOOperationProcessMix:
            outWillDo = mVolumeControl.WillApplyVolumeToAudioRT();
            outWillDoInPlace = true;
            break;

		case kAudioServerPlugInIOOperationCycle:
        case kAudioServerPlugInIOOperationConvertInput:
        case kAudioServerPlugInIOOperationProcessInput:
		case kAudioServerPlugInIOOperationMixOutput:
		case kAudioServerPlugInIOOperationConvertMix:
		default:
			outWillDo = false;
			outWillDoInPlace = true;
			break;
			
	};
}

void	RDC_Device::BeginIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID)
{
	#pragma unused(inIOBufferFrameSize, inIOCycleInfo)
    
    if(inOperationID == kAudioServerPlugInIOOperationThread)
    {
        // Update this client's IO state and send notifications if that changes the value of
        // kAudioDeviceCustomPropertyDeviceIsRunning or
        // kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanRDCApp. We have to do this here
        // as well as in StartIO because the HAL only calls StartIO/StopIO with the first/last clients.
        //
        // We perform the update async because it isn't real-time safe, but we can't just dispatch it with
        // dispatch_async because that isn't real-time safe either. (Apparently even constructing a block
        // isn't.)
        //
        // We don't have to hold the IO mutex here because mTaskQueue and mClients don't change and
        // adding a task to mTaskQueue is thread safe.
        mTaskQueue.QueueAsync_StartClientIO(&mClients, inClientID);
    }
}

void	RDC_Device::DoIOOperation(AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
    #pragma unused(inStreamObjectID, ioSecondaryBuffer)
    
	switch(inOperationID)
	{
		case kAudioServerPlugInIOOperationReadInput:
            {
                CAMutex::Locker theIOLocker(mIOMutex);

                // Copy the audio data out of our ring buffer.
                //
                // Take the IO mutex because, in testing, not taking it seemed to make this function
                // occasionally miss its deadline and cause an audio glitch. It's hard to be sure
                // that was actually the cause, but it's probably not worth the risk anyway.
                //
                // If an IO operation misses its deadline, the host will log this message:
                //     Audio IO Overload inputs: '<private>' outputs: '<private>' cause: 'Unknown'
                //     prewarming: no recovering: no
                ReadInputData(inIOBufferFrameSize,
                              inIOCycleInfo.mInputTime.mSampleTime,
                              ioMainBuffer);
            }
			break;
            
        case kAudioServerPlugInIOOperationProcessOutput:
            ApplyVolume(inClientID, inIOBufferFrameSize, ioMainBuffer);
            break;

        case kAudioServerPlugInIOOperationProcessMix:
            {
                // Check the arguments.
                ThrowIfNULL(ioMainBuffer,
                            CAException(kAudioHardwareIllegalOperationError),
                            "RDC_Device::DoIOOperation: Buffer for "
                                    "kAudioServerPlugInIOOperationProcessMix must not be null");

                CAMutex::Locker theIOLocker(mIOMutex);

                // We ask to do this IO operation so this device can apply its own volume to the
                // stream. Currently, only the UI sounds device does.
            }
            break;

        case kAudioServerPlugInIOOperationWriteMix:
            {
                CAMutex::Locker theIOLocker(mIOMutex);

                // Copy the audio data into our ring buffer.
                WriteOutputData(inIOBufferFrameSize,
                                inIOCycleInfo.mOutputTime.mSampleTime,
                                ioMainBuffer);
            }
			break;

		default:
            // Note that this will only log the error in debug builds.
			DebugMsg("RDC_Device::DoIOOperation: Unexpected IO operation: %u", inOperationID);
			break;
	};
}

void	RDC_Device::EndIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID)
{
    #pragma unused(inIOBufferFrameSize, inIOCycleInfo)

    if(inOperationID == kAudioServerPlugInIOOperationThread)
    {
        // Tell RDC_Clients that this client has stopped IO. Queued async because we have to be real-time safe here.
        //
        // We don't have to hold the IO mutex here because mTaskQueue and mClients don't change and adding a task to
        // mTaskQueue is thread safe.
        mTaskQueue.QueueAsync_StopClientIO(&mClients, inClientID);
    }
}

void	RDC_Device::ReadInputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* outBuffer)
{
    // Wrap the provided buffer in an AudioBufferList.
    AudioBufferList abl = {
        .mNumberBuffers = 1,
        .mBuffers[0] = {
            .mNumberChannels = 2,
            // Each frame is 2 Float32 samples (one per channel). The number of frames * the number
            // of bytes per frame = the size of outBuffer in bytes.
            .mDataByteSize = static_cast<UInt32>(inIOBufferFrameSize * sizeof(Float32) * 2),
            .mData = outBuffer
        }
    };

    // Copy the audio data from our ring buffer into the provided buffer.
    CARingBufferError err =
            mLoopbackRingBuffer.Fetch(&abl,
                                      inIOBufferFrameSize,
                                      static_cast<CARingBuffer::SampleTime>(inSampleTime));

    // Handle errors.
    switch (err)
    {
        case kCARingBufferError_CPUOverload:
            // Write silence to the buffer.
            memset(outBuffer, 0, abl.mBuffers[0].mDataByteSize);
            break;
        case kCARingBufferError_TooMuch:
            // Should be impossible, but handle it just in case. Write silence to the buffer and
            // return an error code.
            memset(outBuffer, 0, abl.mBuffers[0].mDataByteSize);
            Throw(CAException(kAudioHardwareIllegalOperationError));
        case kCARingBufferError_OK:
            break;
        default:
            throw CAException(kAudioHardwareUnspecifiedError);
    }
}

void	RDC_Device::WriteOutputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer)
{
    // Wrap the provided buffer in an AudioBufferList.
    AudioBufferList abl = {
        .mNumberBuffers = 1,
        .mBuffers[0] = {
            .mNumberChannels = 2,
            // Each frame is 2 Float32 samples (one per channel). The number of frames * the number
            // of bytes per frame = the size of inBuffer in bytes.
            .mDataByteSize = static_cast<UInt32>(inIOBufferFrameSize * sizeof(Float32) * 2),
            .mData = const_cast<void *>(inBuffer)
        }
    };

    // Copy the audio data from the provided buffer into our ring buffer.
    CARingBufferError err =
            mLoopbackRingBuffer.Store(&abl,
                                      inIOBufferFrameSize,
                                      static_cast<CARingBuffer::SampleTime>(inSampleTime));

    // Return an error code if we failed to store the data. (But ignore CPU overload, which would be
    // temporary.)
    if (err != kCARingBufferError_OK && err != kCARingBufferError_CPUOverload)
    {
        Throw(CAException(err));
    }
}

#pragma mark Accessors

void    RDC_Device::RequestEnabledControls(bool inVolumeEnabled, bool inMuteEnabled)
{
    CAMutex::Locker theStateLocker(mStateMutex);

    bool changeVolume = (mVolumeControl.IsActive() != inVolumeEnabled);
    bool changeMute = (mMuteControl.IsActive() != inMuteEnabled);

    if(changeVolume)
    {
        DebugMsg("RDC_Device::RequestEnabledControls: %s volume control",
                 (inVolumeEnabled ? "Enabling" : "Disabling"));
        mPendingOutputVolumeControlEnabled = inVolumeEnabled;
    }

    if(changeMute)
    {
        DebugMsg("RDC_Device::RequestEnabledControls: %s mute control",
                 (inMuteEnabled ? "Enabling" : "Disabling"));
        mPendingOutputMuteControlEnabled = inMuteEnabled;
    }

    if(changeVolume || changeMute)
    {
        // Ask the host to stop IO (and whatever else) so we can safely update the device's list of
        // controls. See RequestDeviceConfigurationChange in AudioServerPlugIn.h.
        AudioObjectID theDeviceObjectID = GetObjectID();
        UInt64 action = static_cast<UInt64>(ChangeAction::SetEnabledControls);
        
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
            RDC_PlugIn::Host_RequestDeviceConfigurationChange(theDeviceObjectID, action, nullptr);
        });
    }
}

Float64	RDC_Device::GetSampleRate() const
{
    // The sample rate is guarded by the state lock. Note that we don't need to take the IO lock.
    CAMutex::Locker theStateLocker(mStateMutex);

    Float64 theSampleRate;

    // Report the sample rate from the wrapped device if we have one. Note that _HW_GetSampleRate
    // the device's nominal sample rate, not one calculated from its timestamps.
    if(mWrappedAudioEngine == nullptr)
    {
        theSampleRate = mLoopbackSampleRate;
    }
    else
    {
        theSampleRate = _HW_GetSampleRate();
    }

    return theSampleRate;
}

void	RDC_Device::RequestSampleRate(Float64 inRequestedSampleRate)
{
    // Changing the sample rate needs to be handled via the RequestConfigChange/PerformConfigChange
    // machinery. See RequestDeviceConfigurationChange in AudioServerPlugIn.h.

	// We try to support any sample rate a real output device might.
    ThrowIf(inRequestedSampleRate < 1.0,
            CAException(kAudioDeviceUnsupportedFormatError),
            "RDC_Device::RequestSampleRate: unsupported sample rate");

    DebugMsg("RDC_Device::RequestSampleRate: Sample rate change requested: %f",
             inRequestedSampleRate);

    CAMutex::Locker theStateLocker(mStateMutex);

    if(inRequestedSampleRate != GetSampleRate())  // Check the sample rate will actually be changed.
    {
        mPendingSampleRate = inRequestedSampleRate;

        // Dispatch this so the change can happen asynchronously.
        auto requestSampleRate = ^{
			UInt64 action = static_cast<UInt64>(ChangeAction::SetSampleRate);
            RDC_PlugIn::Host_RequestDeviceConfigurationChange(GetObjectID(), action, nullptr);
        };

        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, requestSampleRate);
    }
}

RDC_Object&  RDC_Device::GetOwnedObjectByID(AudioObjectID inObjectID)
{
	// C++ is weird. See "Avoid Duplication in const and Non-const Member Functions" in Item 3 of Effective C++.
	return const_cast<RDC_Object&>(static_cast<const RDC_Device&>(*this).GetOwnedObjectByID(inObjectID));
}

const RDC_Object&  RDC_Device::GetOwnedObjectByID(AudioObjectID inObjectID) const
{
	if(inObjectID == mInputStream.GetObjectID())
	{
		return mInputStream;
	}
	else if(inObjectID == mOutputStream.GetObjectID())
	{
		return mOutputStream;
	}
	else if(inObjectID == mVolumeControl.GetObjectID())
	{
		return mVolumeControl;
	}
	else if(inObjectID == mMuteControl.GetObjectID())
	{
		return mMuteControl;
	}
	else
	{
		LogError("RDC_Device::GetOwnedObjectByID: Unknown object ID. inObjectID = %u", inObjectID);
		Throw(CAException(kAudioHardwareBadObjectError));
	}
}

UInt32	RDC_Device::GetNumberOfSubObjects() const
{
	return kNumberOfInputSubObjects + GetNumberOfOutputSubObjects();
}

UInt32	RDC_Device::GetNumberOfOutputSubObjects() const
{
	return kNumberOfOutputStreams + GetNumberOfOutputControls();
}

UInt32	RDC_Device::GetNumberOfOutputControls() const
{
	CAMutex::Locker theStateLocker(mStateMutex);

	UInt32 theAnswer = 0;

	if(mVolumeControl.IsActive())
	{
		theAnswer++;
	}

	if(mMuteControl.IsActive())
	{
		theAnswer++;
	}

    return theAnswer;
}

void    RDC_Device::SetEnabledControls(bool inVolumeEnabled, bool inMuteEnabled)
{
    CAMutex::Locker theStateLocker(mStateMutex);

    if(mVolumeControl.IsActive() != inVolumeEnabled)
    {
        DebugMsg("RDC_Device::SetEnabledControls: %s the volume control",
                 inVolumeEnabled ? "Enabling" : "Disabling");

        if(inVolumeEnabled)
		{
			mVolumeControl.Activate();
		}
		else
		{
			mVolumeControl.Deactivate();
		}
    }

    if(mMuteControl.IsActive() != inMuteEnabled)
    {
        DebugMsg("RDC_Device::SetEnabledControls: %s the mute control",
                 inMuteEnabled ? "Enabling" : "Disabling");

        if(inMuteEnabled)
		{
			mMuteControl.Activate();
		}
		else
		{
			mMuteControl.Deactivate();
		}
    }
}

void RDC_Device::SetSampleRate(Float64 inSampleRate, bool force)
{
    // We try to support any sample rate a real output device might.
    ThrowIf(inSampleRate < 1.0,
            CAException(kAudioDeviceUnsupportedFormatError),
            "RDC_Device::SetSampleRate: unsupported sample rate");

    CAMutex::Locker theStateLocker(mStateMutex);

    Float64 theCurrentSampleRate = GetSampleRate();

    if((inSampleRate != theCurrentSampleRate) || force)  // Check whether we need to change it.
    {
        DebugMsg("RDC_Device::SetSampleRate: Changing the sample rate from %f to %f",
                 theCurrentSampleRate,
                 inSampleRate);

        // Update the sample rate on the wrapped device if we have one.
        if(mWrappedAudioEngine != nullptr)
        {
            kern_return_t theError = _HW_SetSampleRate(inSampleRate);
            ThrowIfKernelError(theError,
                               CAException(kAudioHardwareUnspecifiedError),
                               "RDC_Device::SetSampleRate: Error setting the sample rate on the "
                               "wrapped audio device.");
        }

        // Update the sample rate for loopback.
        mLoopbackSampleRate = inSampleRate;
        InitLoopback();

        // Update the streams.
        mInputStream.SetSampleRate(inSampleRate);
        mOutputStream.SetSampleRate(inSampleRate);
    }
    else
    {
        DebugMsg("RDC_Device::SetSampleRate: The sample rate is already set to %f", inSampleRate);
    }
}

bool    RDC_Device::IsStreamID(AudioObjectID inObjectID) const noexcept
{
    return (inObjectID == mInputStream.GetObjectID()) || (inObjectID == mOutputStream.GetObjectID());
}

#pragma mark Hardware Accessors

// TODO: Out of laziness, some of these hardware functions do more than their names suggest

void	RDC_Device::_HW_Open()
{
}

void	RDC_Device::_HW_Close()
{
}

kern_return_t	RDC_Device::_HW_StartIO()
{
	RDCAssert(mStateMutex.IsOwnedByCurrentThread(),
              "RDC_Device::_HW_StartIO: Called without taking the state mutex");

    if(mWrappedAudioEngine != nullptr)
    {
    }
    
    // Reset the loopback timing values
    mLoopbackTime.numberTimeStamps = 0;
    mLoopbackTime.anchorHostTime = CAHostTimeBase::GetTheCurrentTime();
	RDCAssert(mIOMutex.IsFree(), "RDC_Device::_HW_StartIO: IO mutex taken before starting IO");
    
    return KERN_SUCCESS;
}

void	RDC_Device::_HW_StopIO()
{
    if(mWrappedAudioEngine != NULL)
    {
    }
}

Float64	RDC_Device::_HW_GetSampleRate() const
{
    // This function should only be called when wrapping a device.
    ThrowIf(mWrappedAudioEngine == nullptr,
            CAException(kAudioHardwareUnspecifiedError),
            "RDC_Device::_HW_GetSampleRate: No wrapped audio device");

    return mWrappedAudioEngine->GetSampleRate();
}

kern_return_t	RDC_Device::_HW_SetSampleRate(Float64 inNewSampleRate)
{
    // This function should only be called when wrapping a device.
    ThrowIf(mWrappedAudioEngine == nullptr,
            CAException(kAudioHardwareUnspecifiedError),
            "RDC_Device::_HW_SetSampleRate: No wrapped audio device");

    return mWrappedAudioEngine->SetSampleRate(inNewSampleRate);
}

UInt32	RDC_Device::_HW_GetRingBufferFrameSize() const
{
    return (mWrappedAudioEngine != NULL) ? mWrappedAudioEngine->GetSampleBufferFrameSize() : 0;
}

#pragma mark Implementation

void	RDC_Device::AddClient(const AudioServerPlugInClientInfo* inClientInfo)
{
    DebugMsg("RDC_Device::AddClient: Adding client %u (%s)",
             inClientInfo->mClientID,
             (inClientInfo->mBundleID == NULL ?
                 "no bundle ID" :
                 CFStringGetCStringPtr(inClientInfo->mBundleID, kCFStringEncodingUTF8)));
    
    CAMutex::Locker theStateLocker(mStateMutex);

    mClients.AddClient(inClientInfo);
}

void	RDC_Device::RemoveClient(const AudioServerPlugInClientInfo* inClientInfo)
{
    DebugMsg("RDC_Device::RemoveClient: Removing client %u (%s)",
             inClientInfo->mClientID,
             CFStringGetCStringPtr(inClientInfo->mBundleID, kCFStringEncodingUTF8));
    
    CAMutex::Locker theStateLocker(mStateMutex);

    mClients.RemoveClient(inClientInfo->mClientID);
}

void	RDC_Device::PerformConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeInfo)
    DebugMsg("RDC_Device::PerformConfigChange: inChangeAction = %llu", inChangeAction);

    // Apply a change requested with RDC_PlugIn::Host_RequestDeviceConfigurationChange. See
    // PerformDeviceConfigurationChange in AudioServerPlugIn.h.

    switch(static_cast<ChangeAction>(inChangeAction))
    {
        case ChangeAction::SetSampleRate:
            SetSampleRate(mPendingSampleRate);
            break;

        case ChangeAction::SetEnabledControls:
            SetEnabledControls(mPendingOutputVolumeControlEnabled,
                               mPendingOutputMuteControlEnabled);
            break;
    }
}

void	RDC_Device::AbortConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeAction, inChangeInfo)
	
	//	this device doesn't need to do anything special if a change request gets aborted
}


void    RDC_Device::ApplyVolume(UInt32 inClientID, UInt32 inIOBufferFrameSize, void* ioBuffer) const
{
    mVolumeControl.ApplyVolumeToAudioRT(reinterpret_cast<Float32*>(ioBuffer),
                                        inIOBufferFrameSize);
}
