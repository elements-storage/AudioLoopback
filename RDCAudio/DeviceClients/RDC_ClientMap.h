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
//  RDC_ClientMap.h
//  RDCDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __RDCDriver__RDC_ClientMap__
#define __RDCDriver__RDC_ClientMap__

// Local Includes
#include "RDC_Client.h"
#include "RDC_TaskQueue.h"

// PublicUtility Includes
#include "CAMutex.h"
#include "CAVolumeCurve.h"

// STL Includes
#include <map>
#include <vector>
#include <functional>


// Forward Declarations
class RDC_ClientTasks;


#pragma clang assume_nonnull begin

//==================================================================================================
//	RDC_ClientMap
//
//  This class stores the clients (RDC_Client) that have been registered with RDCDevice by the HAL.
//  It also maintains maps from clients' PIDs and bundle IDs to the clients. When a client is
//  removed by the HAL we add it to a map of past clients to keep track of settings specific to that
//  client. (Currently only the client's volume.)
//
//  Since the maps are read from during IO, this class has to to be real-time safe when accessing
//  them. So each map has an identical "shadow" map, which we use to buffer updates.
//
//  To update the clients we lock the shadow maps, modify them, have RDC_TaskQueue's real-time
//  thread swap them with the main maps, and then repeat the modification to keep both sets of maps
//  identical. We have to swap the maps on a real-time thread so we can take the main maps' lock
//  without risking priority inversion, but this way the actual work doesn't need to be real-time
//  safe.
//
//  Methods that only read from the maps and are called on non-real-time threads will just read
//  from the shadow maps because it's easier.
//
//  Methods whose names end with "RT" and "NonRT" can only safely be called from real-time and
//  non-real-time threads respectively. (Methods with neither are most likely non-RT.)
//==================================================================================================

class RDC_ClientMap
{
    
    friend class RDC_ClientTasks;
    
    typedef std::vector<RDC_Client*> RDC_ClientPtrList;
    
public:
                                                        RDC_ClientMap(RDC_TaskQueue* inTaskQueue) : mTaskQueue(inTaskQueue), mMapsMutex("Maps mutex"), mShadowMapsMutex("Shadow maps mutex") { };

    void                                                AddClient(RDC_Client inClient);
    
private:
    void                                                AddClientToShadowMaps(RDC_Client inClient);
    
public:
    // Returns the removed client
    RDC_Client                                          RemoveClient(UInt32 inClientID);
    
    // These methods are functionally identical except that GetClientRT must only be called from real-time threads and GetClientNonRT
    // must only be called from non-real-time threads. Both return true if a client was found.
    bool                                                GetClientRT(UInt32 inClientID, RDC_Client* outClient) const;
    bool                                                GetClientNonRT(UInt32 inClientID, RDC_Client* outClient) const;
    
private:
    static bool                                         GetClient(const std::map<UInt32, RDC_Client>& inClientMap,
                                                                  UInt32 inClientID,
                                                                  RDC_Client* outClient);
    
public:
    std::vector<RDC_Client>                             GetClientsByPID(pid_t inPID) const;
    
public:
    void                                                StartIONonRT(UInt32 inClientID) { UpdateClientIOStateNonRT(inClientID, true); }
    void                                                StopIONonRT(UInt32 inClientID) { UpdateClientIOStateNonRT(inClientID, false); }
    
private:
    void                                                UpdateClientIOStateNonRT(UInt32 inClientID, bool inDoingIO);
    
    // Has a real-time thread call SwapInShadowMapsRT. (Synchronously queues the call as a task on mTaskQueue.)
    // The shadow maps mutex must be locked when calling this method.
    void                                                SwapInShadowMaps();
    // Note that this method is called by RDC_TaskQueue through the RDC_ClientTasks interface. The shadow maps
    // mutex must be locked when calling this method.
    void                                                SwapInShadowMapsRT();
    
    // Client lookup for PID inAppPID
    std::vector<RDC_Client*> * _Nullable                GetClients(pid_t inAppPid);
    // Client lookup for bundle ID inAppBundleID
    std::vector<RDC_Client*> * _Nullable                GetClients(CACFString inAppBundleID);
    
private:
    RDC_TaskQueue*                                      mTaskQueue;
    
    // Must be held to access mClientMap or mClientMapByPID. Code that runs while holding this mutex needs
    // to be real-time safe. Should probably not be held for most operations on mClientMapByBundleID because,
    // as far as I can tell, code that works with CFStrings is unlikely to be real-time safe.
    CAMutex                                             mMapsMutex;
    // Should only be locked by non-real-time threads. Should not be released until the maps have been
    // made identical to their shadow maps.
    CAMutex                                             mShadowMapsMutex;
    
    // The clients currently registered with RDCDevice. Indexed by client ID.
    std::map<UInt32, RDC_Client>                        mClientMap;
    // We keep this in sync with mClientMap so it can be modified outside of real-time safe sections and
    // then swapped in on a real-time thread, which is safe.
    std::map<UInt32, RDC_Client>                        mClientMapShadow;
    
    // These maps hold lists of pointers to clients in mClientMap/mClientMapShadow. Lists because a process
    // can have multiple clients and clients can have the same bundle ID.
    
    std::map<pid_t, RDC_ClientPtrList>                  mClientMapByPID;
    std::map<pid_t, RDC_ClientPtrList>                  mClientMapByPIDShadow;
    
    std::map<CACFString, RDC_ClientPtrList>             mClientMapByBundleID;
    std::map<CACFString, RDC_ClientPtrList>             mClientMapByBundleIDShadow;
    
    // Clients are added to mPastClientMap so we can restore settings specific to them if they get
    // added again.
    std::map<CACFString, RDC_Client>                    mPastClientMap;
    
};

#pragma clang assume_nonnull end

#endif /* __RDCDriver__RDC_ClientMap__ */

