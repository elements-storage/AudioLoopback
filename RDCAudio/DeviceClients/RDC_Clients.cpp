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
//  RDC_Clients.cpp
//  RDCDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "RDC_Clients.h"

// Local Includes
#include "RDC_Types.h"
#include "RDC_PlugIn.h"

// PublicUtility Includes
#include "CAException.h"
#include "CADispatchQueue.h"


#pragma mark Construction/Destruction

RDC_Clients::RDC_Clients(AudioObjectID inOwnerDeviceID, RDC_TaskQueue* inTaskQueue)
:
    mOwnerDeviceID(inOwnerDeviceID),
    mClientMap(inTaskQueue)
{
}

#pragma mark Add/Remove Clients

void    RDC_Clients::AddClient(RDC_Client inClient)
{
    CAMutex::Locker theLocker(mMutex);
    
    mClientMap.AddClient(inClient);    
}

void    RDC_Clients::RemoveClient(const UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    mClientMap.RemoveClient(inClientID);
}

#pragma mark IO Status

bool    RDC_Clients::StartIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    bool didStartIO = false;
    
    RDC_Client theClient;
    bool didFindClient = mClientMap.GetClientNonRT(inClientID, &theClient);
    
    ThrowIf(!didFindClient, RDC_InvalidClientException(), "RDC_Clients::StartIO: Cannot start IO for client that was never added");
    
    bool sendIsRunningNotification = false;

    if(!theClient.mDoingIO)
    {
        // Make sure we can start
        ThrowIf(mStartCount == UINT64_MAX, CAException(kAudioHardwareIllegalOperationError), "RDC_Clients::StartIO: failed to start because the ref count was maxxed out already");
        
        DebugMsg("RDC_Clients::StartIO: Client %u (%s, %d) starting IO",
                 inClientID,
                 CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
                 theClient.mProcessID);
        
        mClientMap.StartIONonRT(inClientID);
        
        mStartCount++;
        
        // Return true if no other clients were running IO before this one started, which means the device should start IO
        didStartIO = (mStartCount == 1);
        sendIsRunningNotification = didStartIO;
    }
    
    SendIORunningNotifications(sendIsRunningNotification, false);

    return didStartIO;
}

bool    RDC_Clients::StopIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    bool didStopIO = false;
    
    RDC_Client theClient;
    bool didFindClient = mClientMap.GetClientNonRT(inClientID, &theClient);
    
    ThrowIf(!didFindClient, RDC_InvalidClientException(), "RDC_Clients::StopIO: Cannot stop IO for client that was never added");
    
    bool sendIsRunningNotification = false;
    
    if(theClient.mDoingIO)
    {
        DebugMsg("RDC_Clients::StopIO: Client %u (%s, %d) stopping IO",
                 inClientID,
                 CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
                 theClient.mProcessID);
        
        mClientMap.StopIONonRT(inClientID);
        
        ThrowIf(mStartCount <= 0, CAException(kAudioHardwareIllegalOperationError), "RDC_Clients::StopIO: Underflowed mStartCount");
        
        mStartCount--;
        
        // Return true if we stopped IO entirely (i.e. there are no clients still running IO)
        didStopIO = (mStartCount == 0);
        sendIsRunningNotification = didStopIO;
    }
    
    SendIORunningNotifications(sendIsRunningNotification, false);
    
    return didStopIO;
}

bool    RDC_Clients::ClientsRunningIO() const
{
    return mStartCount > 0;
}

void    RDC_Clients::SendIORunningNotifications(bool sendIsRunningNotification, bool sendIsRunningSomewhereOtherThanRDCAppNotification) const
{
    if(sendIsRunningNotification)
    {
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
            AudioObjectPropertyAddress theChangedProperties[2];
            UInt32 theNotificationCount = 0;

            if(sendIsRunningNotification)
            {
                DebugMsg("RDC_Clients::SendIORunningNotifications: Sending kAudioDevicePropertyDeviceIsRunning");
                theChangedProperties[0] = { kAudioDevicePropertyDeviceIsRunning, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
                theNotificationCount++;
            }

            RDC_PlugIn::Host_PropertiesChanged(mOwnerDeviceID, theNotificationCount, theChangedProperties);
        });
    }
}
