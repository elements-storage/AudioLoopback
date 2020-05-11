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
//  RDC_WrappedAudioEngine.h
//  RDCDriver
//
//  Copyright © 2016 Kyle Neideck
//
//  The plan for this is to allow devices with IOAudioEngine drivers to be used as the output device
//  directly from RDCDriver, rather than going through RDCApp. That way we get roughly the same CPU
//  usage and latency as normal, and don't need to worry about pausing RDCApp's IO when no clients
//  are doing IO. It also lets RDCDriver mostly continue working without RDCApp running. I've written
//  a very experimental version that mostly works but the code needs a lot of clean up so I haven't
//  added it to this project yet.
//

#ifndef __RDCDriver__RDC_WrappedAudioEngine__
#define __RDCDriver__RDC_WrappedAudioEngine__

#include <CoreAudio/CoreAudioTypes.h>
#include <mach/kern_return.h>


class RDC_WrappedAudioEngine
{
    
public:
    UInt64          GetSampleRate() const;
    kern_return_t   SetSampleRate(Float64 inNewSampleRate);
    UInt32          GetSampleBufferFrameSize() const;
    
};

#endif /* __RDCDriver__RDC_WrappedAudioEngine__ */

