#pragma once
#if !defined(__COREAUDIO_USE_FLAT_INCLUDES__)
	#include <CoreAudio/CoreAudioTypes.h>
#else
	#include <CoreAudioTypes.h>
#endif

#include <atomic>

enum {
	kCARingBufferError_OK = 0,
	kCARingBufferError_TooMuch = 3, // fetch start time is earlier than buffer start time and fetch end time is later than buffer end time
	kCARingBufferError_CPUOverload = 4 // the reader is unable to get enough CPU cycles to capture a consistent snapshot of the time bounds
};

typedef SInt32 CARingBufferError;

const UInt32 kGeneralRingTimeBoundsQueueSize = 32;
const UInt32 kGeneralRingTimeBoundsQueueMask = kGeneralRingTimeBoundsQueueSize - 1;

class CARingBuffer {
public:
	typedef SInt64 SampleTime;

	CARingBuffer();
	~CARingBuffer();
	
	void					Allocate(int nChannels, UInt32 bytesPerFrame, UInt32 capacityFrames);
								// capacityFrames will be rounded up to a power of 2
	void					Deallocate();
	
	CARingBufferError	Store(const AudioBufferList *abl, UInt32 nFrames, SampleTime frameNumber);
							// Copy nFrames of data into the ring buffer at the specified sample time.
							// The sample time should normally increase sequentially, though gaps
							// are filled with zeroes. A sufficiently large gap effectively empties
							// the buffer before storing the new data. 
							
							// If frameNumber is less than the previous frame number, the behavior is undefined.
							
							// Return false for failure (buffer not large enough).
				
	CARingBufferError	Fetch(AudioBufferList *abl, UInt32 nFrames, SampleTime frameNumber);
								// will alter mDataByteSize of the buffers
	
	CARingBufferError	GetTimeBounds(SampleTime &startTime, SampleTime &endTime);
	
protected:

	UInt32					FrameOffset(SampleTime frameNumber) { return (frameNumber & mCapacityFramesMask) * mBytesPerFrame; }

	CARingBufferError		ClipTimeBounds(SampleTime& startRead, SampleTime& endRead);
	
	// these should only be called from Store.
	SampleTime				StartTime() const { return mTimeBoundsQueue[mTimeBoundsQueuePtr & kGeneralRingTimeBoundsQueueMask].mStartTime; }
	SampleTime				EndTime()   const { return mTimeBoundsQueue[mTimeBoundsQueuePtr & kGeneralRingTimeBoundsQueueMask].mEndTime; }
	void					SetTimeBounds(SampleTime startTime, SampleTime endTime);
	
protected:
	Byte **					mBuffers;				// allocated in one chunk of memory
	int						mNumberChannels;
	UInt32					mBytesPerFrame;			// within one deinterleaved channel
	UInt32					mCapacityFrames;		// per channel, must be a power of 2
	UInt32					mCapacityFramesMask;
	UInt32					mCapacityBytes;			// per channel
	
	// range of valid sample time in the buffer
	typedef struct {
		volatile SampleTime		mStartTime;
		volatile SampleTime		mEndTime;
		volatile UInt32			mUpdateCounter;
	} TimeBounds;
	
	CARingBuffer::TimeBounds mTimeBoundsQueue[kGeneralRingTimeBoundsQueueSize];
	std::atomic<UInt32> mTimeBoundsQueuePtr;
};



