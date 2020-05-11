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
//  RDC_ClientTasks.h
//  RDCDriver
//
//  Copyright © 2016 Kyle Neideck
//
//  The interface between the client classes (RDC_Client, RDC_Clients and RDC_ClientMap) and RDC_TaskQueue.
//

#ifndef __RDCDriver__RDC_ClientTasks__
#define __RDCDriver__RDC_ClientTasks__

// Local Includes
#include "RDC_Clients.h"
#include "RDC_ClientMap.h"


// Forward Declarations
class RDC_TaskQueue;


#pragma clang assume_nonnull begin

class RDC_ClientTasks
{
    
    friend class RDC_TaskQueue;
    
private:
    static bool                            StartIONonRT(RDC_Clients* inClients, UInt32 inClientID) { return inClients->StartIONonRT(inClientID); }
    static bool                            StopIONonRT(RDC_Clients* inClients, UInt32 inClientID) { return inClients->StopIONonRT(inClientID); }
    
    static void                            SwapInShadowMapsRT(RDC_ClientMap* inClientMap) { inClientMap->SwapInShadowMapsRT(); }
    
};

#pragma clang assume_nonnull end

#endif /* __RDCDriver__RDC_ClientTasks__ */

