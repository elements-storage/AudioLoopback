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
//  RDC_Clients.h
//  RDCDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __RDCDriver__RDC_Clients__
#define __RDCDriver__RDC_Clients__

// Local Includes
#include "RDC_Client.h"
#include "RDC_ClientMap.h"

// PublicUtility Includes
#include "CAVolumeCurve.h"
#include "CAMutex.h"

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


// Forward Declations
class RDC_ClientTasks;


#pragma clang assume_nonnull begin

//==================================================================================================
//	RDC_Clients
//
//  Holds information about the clients (of the host) of the RDCDevice, i.e. the apps registered
//  with the HAL, generally so they can do IO at some point. RDCApp and the music player are special
//  case clients.
//
//  Methods whose names end with "RT" should only be called from real-time threads.
//==================================================================================================

class RDC_Clients
{
    
    friend class RDC_ClientTasks;
    
public:
                                        RDC_Clients(AudioObjectID inOwnerDeviceID, RDC_TaskQueue* inTaskQueue);
                                        ~RDC_Clients() = default;
    // Disallow copying. (It could make sense to implement these in future, but we don't need them currently.)
                                        RDC_Clients(const RDC_Clients&) = delete;
                                        RDC_Clients& operator=(const RDC_Clients&) = delete;
    
    void                                AddClient(RDC_Client inClient);
    void                                RemoveClient(const UInt32 inClientID);
    
private:
    // Only RDC_TaskQueue is allowed to call these (through the RDC_ClientTasks interface). We get notifications
    // from the HAL when clients start/stop IO and they have to be processed in the order we receive them to
    // avoid race conditions. If these methods could be called directly those calls would skip any queued calls.
    bool                                StartIONonRT(UInt32 inClientID);
    bool                                StopIONonRT(UInt32 inClientID);

public:
    bool                                ClientsRunningIO() const;
    
private:
    void                                SendIORunningNotifications(bool sendIsRunningNotification, bool sendIsRunningSomewhereOtherThanRDCAppNotification) const;
            
private:
    AudioObjectID                       mOwnerDeviceID;
    RDC_ClientMap                       mClientMap;
    
    // Counters for the number of clients that are doing IO. These are used to tell whether any clients
    // are currently doing IO without having to check every client's mDoingIO.
    //
    // We need to reference count this rather than just using a bool because the HAL might (but usually
    // doesn't) call our StartIO/StopIO functions for clients other than the first to start and last to
    // stop.
    UInt64                              mStartCount = 0;
    
    CAMutex                             mMutex { "Clients" };
};

#pragma clang assume_nonnull end

#endif /* __RDCDriver__RDC_Clients__ */

