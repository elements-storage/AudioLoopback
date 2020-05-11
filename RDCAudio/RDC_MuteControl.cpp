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
//  RDC_MuteControl.cpp
//  RDCDriver
//
//  Copyright © 2016, 2017 Kyle Neideck
//

// Self Include
#include "RDC_MuteControl.h"

// Local Includes
#include "RDC_PlugIn.h"

// PublicUtility Includes
#include "CADebugMacros.h"
#include "CAException.h"
#include "CADispatchQueue.h"


#pragma clang assume_nonnull begin

#pragma mark Construction/Destruction

RDC_MuteControl::RDC_MuteControl(AudioObjectID inObjectID,
                                 AudioObjectID inOwnerObjectID,
                                 AudioObjectPropertyScope inScope,
                                 AudioObjectPropertyElement inElement)
:
    RDC_Control(inObjectID,
                kAudioMuteControlClassID,
                kAudioBooleanControlClassID,
                inOwnerObjectID,
                inScope,
                inElement),
    mMutex("Mute Control"),
    mMuted(false)
{
}

#pragma mark Property Operations

bool    RDC_MuteControl::HasProperty(AudioObjectID inObjectID,
                                     pid_t inClientPID,
                                     const AudioObjectPropertyAddress& inAddress) const
{
    CheckObjectID(inObjectID);

    bool theAnswer = false;

    switch(inAddress.mSelector)
    {
        case kAudioBooleanControlPropertyValue:
            theAnswer = true;
            break;

        default:
            theAnswer = RDC_Control::HasProperty(inObjectID, inClientPID, inAddress);
            break;
    };

    return theAnswer;
}

bool    RDC_MuteControl::IsPropertySettable(AudioObjectID inObjectID,
                                            pid_t inClientPID,
                                            const AudioObjectPropertyAddress& inAddress) const
{
    CheckObjectID(inObjectID);

    bool theAnswer = false;

    switch(inAddress.mSelector)
    {
        case kAudioBooleanControlPropertyValue:
            theAnswer = true;
            break;

        default:
            theAnswer = RDC_Control::IsPropertySettable(inObjectID, inClientPID, inAddress);
            break;
    };

    return theAnswer;
}

UInt32  RDC_MuteControl::GetPropertyDataSize(AudioObjectID inObjectID,
                                             pid_t inClientPID,
                                             const AudioObjectPropertyAddress& inAddress,
                                             UInt32 inQualifierDataSize,
                                             const void* inQualifierData) const
{
    CheckObjectID(inObjectID);

    UInt32 theAnswer = 0;

    switch(inAddress.mSelector)
    {
        case kAudioBooleanControlPropertyValue:
            theAnswer = sizeof(UInt32);
            break;

        default:
            theAnswer = RDC_Control::GetPropertyDataSize(inObjectID,
                                                         inClientPID,
                                                         inAddress,
                                                         inQualifierDataSize,
                                                         inQualifierData);
            break;
    };

    return theAnswer;
}

void    RDC_MuteControl::GetPropertyData(AudioObjectID inObjectID,
                                         pid_t inClientPID,
                                         const AudioObjectPropertyAddress& inAddress,
                                         UInt32 inQualifierDataSize,
                                         const void* inQualifierData,
                                         UInt32 inDataSize,
                                         UInt32& outDataSize,
                                         void* outData) const
{
    CheckObjectID(inObjectID);

    switch(inAddress.mSelector)
    {
        case kAudioBooleanControlPropertyValue:
            // This returns the mute value of the control.
            {
                ThrowIf(inDataSize < sizeof(UInt32),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "RDC_MuteControl::GetPropertyData: not enough space for the return value "
                        "of kAudioBooleanControlPropertyValue for the mute control");

                CAMutex::Locker theLocker(mMutex);

                // Non-zero for true, which means audio is being muted.
                *reinterpret_cast<UInt32*>(outData) = mMuted ? 1 : 0;
                outDataSize = sizeof(UInt32);
            }
            break;

        default:
            RDC_Control::GetPropertyData(inObjectID,
                                         inClientPID,
                                         inAddress,
                                         inQualifierDataSize,
                                         inQualifierData,
                                         inDataSize,
                                         outDataSize,
                                         outData);
            break;
    };
}

void    RDC_MuteControl::SetPropertyData(AudioObjectID inObjectID,
                                         pid_t inClientPID,
                                         const AudioObjectPropertyAddress& inAddress,
                                         UInt32 inQualifierDataSize,
                                         const void* inQualifierData,
                                         UInt32 inDataSize,
                                         const void* inData)
{
    CheckObjectID(inObjectID);

    switch(inAddress.mSelector)
    {
        case kAudioBooleanControlPropertyValue:
            {
                ThrowIf(inDataSize < sizeof(UInt32),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "RDC_MuteControl::SetPropertyData: wrong size for the data for "
                        "kAudioBooleanControlPropertyValue");

                CAMutex::Locker theLocker(mMutex);

                // Non-zero for true, meaning audio will be muted.
                bool theNewMuted = (*reinterpret_cast<const UInt32*>(inData) != 0);

                if(mMuted != theNewMuted)
                {
                    mMuted = theNewMuted;

                    // Send notifications.
                    CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
                        AudioObjectPropertyAddress theChangedProperty[1];
                        theChangedProperty[0] = {
                                kAudioBooleanControlPropertyValue, mScope, mElement
                        };

                        RDC_PlugIn::Host_PropertiesChanged(inObjectID, 1, theChangedProperty);
                    });
                }
            }
            break;

        default:
            RDC_Control::SetPropertyData(inObjectID,
                                         inClientPID,
                                         inAddress,
                                         inQualifierDataSize,
                                         inQualifierData,
                                         inDataSize,
                                         inData);
            break;
    };
}

#pragma clang assume_nonnull end

