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
//  RDC_ClientMap.cpp
//  RDCDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "RDC_ClientMap.h"

// Local Includes
#include "RDC_Types.h"

// PublicUtility Includes
#include "CAException.h"


//#pragma clang assume_nonnull begin

void    RDC_ClientMap::AddClient(RDC_Client inClient)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
        
    // Add the new client to the shadow maps
    AddClientToShadowMaps(inClient);
    
    // Swap the maps with their shadow maps
    SwapInShadowMaps();
    
    // The shadow maps (which were the main maps until we swapped them) are now missing the new client. Add it again to
    // keep the sets of maps identical.
    AddClientToShadowMaps(inClient);

    // Insert the client into the past clients map. We do this here rather than in RemoveClient
    // because some apps add multiple clients with the same bundle ID and we want to give them all
    // the same settings (volume, etc.).
    if(inClient.mBundleID.IsValid())
    {
        mPastClientMap[inClient.mBundleID] = inClient;
    }
}

void    RDC_ClientMap::AddClientToShadowMaps(RDC_Client inClient)
{
    ThrowIf(mClientMapShadow.count(inClient.mClientID) != 0,
            RDC_InvalidClientException(),
            "RDC_ClientMap::AddClientToShadowMaps: Tried to add client whose client ID was already in use");
    
    // Add to the client ID shadow map
    mClientMapShadow[inClient.mClientID] = inClient;
    
    // Get a reference to the client in the map so we can add it to the pointer maps
    RDC_Client& clientInMap = mClientMapShadow.at(inClient.mClientID);
    
    // Add to the PID shadow map
    mClientMapByPIDShadow[inClient.mProcessID].push_back(&clientInMap);
    
    // Add to the bundle ID shadow map
    if(inClient.mBundleID.IsValid())
    {
        mClientMapByBundleIDShadow[inClient.mBundleID].push_back(&clientInMap);
    }
}

RDC_Client    RDC_ClientMap::RemoveClient(UInt32 inClientID)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    auto theClientItr = mClientMapShadow.find(inClientID);
    
    // Removing a client that was never added is an error
    ThrowIf(theClientItr == mClientMapShadow.end(),
            RDC_InvalidClientException(),
            "RDC_ClientMap::RemoveClient: Could not find client to be removed");
    
    RDC_Client theClient = theClientItr->second;
    
    // Remove the client from the shadow maps
    mClientMapShadow.erase(theClientItr);
    mClientMapByPIDShadow.erase(theClient.mProcessID);
    if(theClient.mBundleID.IsValid())
    {
        mClientMapByBundleID.erase(theClient.mBundleID);
    }
    
    // Swap the maps with their shadow maps
    SwapInShadowMaps();
    
    // Erase the client again so the maps and their shadow maps are kept identical
    mClientMapShadow.erase(inClientID);
    mClientMapByPIDShadow.erase(theClient.mProcessID);
    if(theClient.mBundleID.IsValid())
    {
        mClientMapByBundleID.erase(theClient.mBundleID);
    }
    
    return theClient;
}

bool    RDC_ClientMap::GetClientRT(UInt32 inClientID, RDC_Client* outClient) const
{
    CAMutex::Locker theMapsLocker(mMapsMutex);
    return GetClient(mClientMap, inClientID, outClient);
}

bool    RDC_ClientMap::GetClientNonRT(UInt32 inClientID, RDC_Client* outClient) const
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    return GetClient(mClientMapShadow, inClientID, outClient);
}

//static
bool    RDC_ClientMap::GetClient(const std::map<UInt32, RDC_Client>& inClientMap, UInt32 inClientID, RDC_Client* outClient)
{
    auto theClientItr = inClientMap.find(inClientID);
    
    if(theClientItr != inClientMap.end())
    {
        *outClient = theClientItr->second;
        return true;
    }
    
    return false;
}

std::vector<RDC_Client> RDC_ClientMap::GetClientsByPID(pid_t inPID) const
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    std::vector<RDC_Client> theClients;
    
    auto theMapItr = mClientMapByPIDShadow.find(inPID);
    if(theMapItr != mClientMapByPIDShadow.end())
    {
        // Found clients with the PID, so copy them into the return vector
        for(auto& theClientPtrsItr : theMapItr->second)
        {
            theClients.push_back(*theClientPtrsItr);
        }
    }
    
    return theClients;
}

template <typename T>
std::vector<RDC_Client*> * _Nullable GetClientsFromMap(std::map<T, std::vector<RDC_Client*>> & map, T key) {
    auto theClientItr = map.find(key);
    if(theClientItr != map.end()) {
        return &theClientItr->second;
    }
    return nullptr;
}

std::vector<RDC_Client*> * _Nullable RDC_ClientMap::GetClients(pid_t inAppPid) {
    return GetClientsFromMap(mClientMapByPIDShadow, inAppPid);
}

std::vector<RDC_Client*> * _Nullable RDC_ClientMap::GetClients(CACFString inAppBundleID) {
    return GetClientsFromMap(mClientMapByBundleIDShadow, inAppBundleID);
}

void    RDC_ClientMap::UpdateClientIOStateNonRT(UInt32 inClientID, bool inDoingIO)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    mClientMapShadow[inClientID].mDoingIO = inDoingIO;
    SwapInShadowMaps();
    mClientMapShadow[inClientID].mDoingIO = inDoingIO;
}

void    RDC_ClientMap::SwapInShadowMaps()
{
    mTaskQueue->QueueSync_SwapClientShadowMaps(this);
}

void    RDC_ClientMap::SwapInShadowMapsRT()
{
#if DEBUG
    // This method should only be called by the realtime worker thread in RDC_TaskQueue. The only safe way to call it is on a realtime
    // thread while a non-realtime thread is holding the shadow maps mutex. (These assertions assume that the realtime worker thread is
    // the only thread we'll call this on, but we could decide to change that at some point.)
    mTaskQueue->AssertCurrentThreadIsRTWorkerThread("RDC_ClientMap::SwapInShadowMapsRT");
    
    Assert(!mShadowMapsMutex.IsFree(), "Can't swap in the shadow maps while the shadow maps mutex is free");
    Assert(!mShadowMapsMutex.IsOwnedByCurrentThread(), "The shadow maps mutex should not be held by a realtime thread");
#endif
    
    CAMutex::Locker theMapsLocker(mMapsMutex);
    
    mClientMap.swap(mClientMapShadow);
    mClientMapByPID.swap(mClientMapByPIDShadow);
    mClientMapByBundleID.swap(mClientMapByBundleIDShadow);
}

//#pragma clang assume_nonnull end

