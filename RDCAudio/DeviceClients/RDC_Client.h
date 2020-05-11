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
//  RDC_Client.h
//  RDCDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __RDCDriver__RDC_Client__
#define __RDCDriver__RDC_Client__

// PublicUtility Includes
#include "CACFString.h"

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


#pragma clang assume_nonnull begin

//==================================================================================================
//	RDC_Client
//
//  Client meaning a client (of the host) of the RDCDevice, i.e. an app registered with the HAL,
//  generally so it can do IO at some point.
//==================================================================================================

class RDC_Client
{
    
public:
                                  RDC_Client() = default;
                                  RDC_Client(const AudioServerPlugInClientInfo* inClientInfo);
                                  ~RDC_Client() = default;
                                  RDC_Client(const RDC_Client& inClient) { Copy(inClient); };
                                  RDC_Client& operator=(const RDC_Client& inClient) { Copy(inClient); return *this; }
    
private:
    void                          Copy(const RDC_Client& inClient);
    
public:
    // These fields are duplicated from AudioServerPlugInClientInfo (except the mBundleID CFStringRef is
    // wrapped in a CACFString here).
    UInt32                        mClientID;
    pid_t                         mProcessID;
    Boolean                       mIsNativeEndian = true;
    CACFString                    mBundleID;
    
    // Becomes true when the client triggers the plugin host to call StartIO or to begin
    // kAudioServerPlugInIOOperationThread, and false again on StopIO or when
    // kAudioServerPlugInIOOperationThread ends
    bool                          mDoingIO = false;
};

#pragma clang assume_nonnull end

#endif /* __RDCDriver__RDC_Client__ */

