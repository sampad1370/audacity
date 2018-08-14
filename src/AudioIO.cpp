/**********************************************************************

  Audacity: A Digital Audio Editor

  AudioIO.cpp

  Copyright 2000-2004:
  Dominic Mazzoni
  Joshua Haberman
  Markus Meyer
  Matt Brubeck

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

********************************************************************//**

\class AudioIO
\brief AudioIO uses the PortAudio library to play and record sound.

  Great care and attention to detail are necessary for understanding and
  modifying this system.  The code in this file is run from three
  different thread contexts: the UI thread, the disk thread (which
  this file creates and maintains; in the code, this is called the
  Audio Thread), and the PortAudio callback thread.
  To highlight this deliniation, the file is divided into three parts
  based on what thread context each function is intended to run in.

  \par EXPERIMENTAL_MIDI_OUT
  If EXPERIMENTAL_MIDI_OUT is defined, this class also manages
  MIDI playback. The reason for putting MIDI here rather than in, say,
  class MidiIO, is that there is no high-level synchronization and
  transport architecture, so Audio and MIDI must be coupled in order
  to start/stop/pause and synchronize them.

  \par MIDI With Audio
  When Audio and MIDI play simultaneously, MIDI synchronizes to Audio.
  This is necessary because the Audio sample clock is not the same
  hardware as the system time used to schedule MIDI messages. MIDI
  is synchronized to Audio because it is simple to pause or rush
  the dispatch of MIDI messages, but generally impossible to pause
  or rush synchronous audio samples (without distortion).

  \par
  MIDI output is driven by yet another thread. In principle, we could
  output timestamped MIDI data at the same time we fill audio buffers
  from disk, but audio buffers are filled far in advance of playback
  time, and there is a lower latency thread (PortAudio's callback) that
  actually sends samples to the output device. The relatively low
  latency to the output device allows Audacity to stop audio output
  quickly. We want the same behavior for MIDI, but there is not
  periodic callback from PortMidi (because MIDI is asynchronous), so
  this function is performed by the MidiThread class.

  \par
  When Audio is running, MIDI is synchronized to Audio. Globals are set
  in the Audio callback (audacityAudioCallback) for use by a time
  function that reports milliseconds to PortMidi. (Details below.)

  \par MIDI Without Audio
  When Audio is not running, PortMidi uses its own millisecond timer
  since there is no audio to synchronize to. (Details below.)

  \par Implementation Notes and Details for MIDI
  When opening devices, successAudio and successMidi indicate errors
  if false, so normally both are true. Use playbackChannels,
  captureChannels and mMidiPlaybackTracks.IsEmpty() to determine if
  Audio or MIDI is actually in use.

  \par Audio Time
  Normally, the current time during playback is given by the variable
  mTime. mTime normally advances by frames / samplerate each time an
  audio buffer is output by the audio callback. However, Audacity has
  a speed control that can perform continuously variable time stretching
  on audio. This is achieved in two places: the playback "mixer" that
  generates the samples for output processes the audio according to
  the speed control. In a separate algorithm, the audio callback updates
  mTime by (frames / samplerate) * factor, where factor reflects the
  speed at mTime. This effectively integrates speed to get position.
  Negative speeds are allowed too, for instance in scrubbing.

  \par The Big Picture
@verbatim

Sample
Time (in seconds, = total_sample_count / sample_rate)
  ^
  |                                             /         /
  |             y=x-mSystemTimeMinusAudioTime /         /
  |                                         /     #   /
  |                                       /         /
  |                                     /   # <- callbacks (#) showing
  |                                   /#        /   lots of timing jitter.
  |       top line is "full buffer" /         /     Some are later,
  |                     condition /         /       indicating buffer is
  |                             /         /         getting low. Plot
  |                           /     #   /           shows sample time
  |                         /    #    /             (based on how many
  |                       /    #    /               samples previously
  |                     /         /                 *written*) vs. real
  |                   / #       /                   time.
  |                 /<------->/ audio latency
  |               /#       v/
  |             /         / bottom line is "empty buffer"
  |           /   #     /      condition = DAC output time =
  |         /         /
  |       /      # <-- rapid callbacks as buffer is filled
  |     /         /
0 +...+---------#---------------------------------------------------->
  0 ^ |         |                                            real time
    | |         first callback time
    | mSystemMinusAudioTime
    |
    Probably the actual real times shown in this graph are very large
    in practice (> 350,000 sec.), so the X "origin" might be when
    the computer was booted or 1970 or something.


@endverbatim

  To estimate the true DAC time (needed to synchronize MIDI), we need
  a mapping from track time to DAC time. The estimate is the theoretical
  time of the full buffer (top diagonal line) + audio latency. To
  estimate the top diagonal line, we "draw" the line to be at least
  as high as any sample time corresponding to a callback (#), and we
  slowly lower the line in case the sample clock is slow or the system
  clock is fast, preventing the estimated line from drifting too far
  from the actual callback observations. The line is occasionally
  "bumped" up by new callback observations, but continuously
  "lowered" at a very low rate.  All adjustment is accomplished
  by changing mSystemMinusAudioTime, shown here as the X-intercept.\n
    theoreticalFullBufferTime = realTime - mSystemMinusAudioTime\n
  To estimate audio latency, notice that the first callback happens on
  an empty buffer, but the buffer soon fills up. This will cause a rapid
  re-estimation of mSystemMinusAudioTime. (The first estimate of
  mSystemMinusAudioTime will simply be the real time of the first
  callback time.) By watching these changes, which happen within ms of
  starting, we can estimate the buffer size and thus audio latency.
  So, to map from track time to real time, we compute:\n
    DACoutputTime = trackTime + mSystemMinusAudioTime\n
  There are some additional details to avoid counting samples while
  paused or while waiting for initialization, MIDI latency, etc.
  Also, in the code, track time is measured with respect to the track
  origin, so there's an extra term to add (mT0) if you start somewhere
  in the middle of the track.
  Finally, when a callback occurs, you might expect there is room in
  the output buffer for the requested frames, so maybe the "full buffer"
  sample time should be based not on the first sample of the callback, but
  the last sample time + 1 sample. I suspect, at least on Linux, that the
  callback occurs as soon as the last callback completes, so the buffer is
  really full, and the callback thread is going to block waiting for space
  in the output buffer.

  \par Midi Time
  MIDI is not warped according to the speed control. This might be
  something that should be changed. (Editorial note: Wouldn't it
  make more sense to display audio at the correct time and allow
  users to stretch audio the way they can stretch MIDI?) For now,
  MIDI plays at 1 second per second, so it requires an unwarped clock.
  In fact, MIDI time synchronization requires a millisecond clock that
  does not pause. Note that mTime will stop progress when the Pause
  button is pressed, even though audio samples (zeros) continue to
  be output.

  \par
  Therefore, we define the following interface for MIDI timing:
  \li \c AudioTime() is the time based on all samples written so far, including zeros output during pauses. AudioTime() is based on the start location mT0, not zero.
  \li \c PauseTime() is the amount of time spent paused, based on a count of zero-padding samples output.
  \li \c MidiTime() is an estimate in milliseconds of the current audio output time + 1s. In other words, what audacity track time corresponds to the audio (plus pause insertions) at the DAC output?

  \par AudioTime() and PauseTime() computation
  AudioTime() is simply mT0 + mNumFrames / mRate.
  mNumFrames is incremented in each audio callback. Similarly, PauseTime()
  is mNumPauseFrames / mRate. mNumPauseFrames is also incremented in
  each audio callback when a pause is in effect or audio output is ready to start.

  \par MidiTime() computation
  MidiTime() is computed based on information from PortAudio's callback,
  which estimates the system time at which the current audio buffer will
  be output. Consider the (unimplemented) function RealToTrack() that
  maps real audio write time to track time. If writeTime is the system
  time for the first sample of the current output buffer, and
  if we are in the callback, so AudioTime() also refers to the first sample
  of the buffer, then \n
  RealToTrack(writeTime) = AudioTime() - PauseTime()\n
  We want to know RealToTrack of the current time (when we are not in the
  callback, so we use this approximation for small d: \n
  RealToTrack(t + d) = RealToTrack(t) + d, or \n
  Letting t = writeTime and d = (systemTime - writeTime), we can
  substitute to get:\n
  RealToTrack(systemTime)
     = RealToTrack(writeTime) + systemTime - writeTime\n
     = AudioTime() - PauseTime() + (systemTime - writeTime) \n
  MidiTime() should include pause time, so that it increases smoothly,
  and audioLatency so that MidiTime() corresponds to the time of audio
  output rather than audio write times.  Also MidiTime() is offset by 1
  second to avoid negative time at startup, so add 1: \n
  MidiTime(systemTime) in seconds\n
     = RealToTrack(systemTime) + PauseTime() - audioLatency + 1 \n
     = AudioTime() + (systemTime - writeTime) - audioLatency + 1 \n
  (Note that audioLatency is called mAudioOutLatency in the code.)
  When we schedule a MIDI event with track time TT, we need
  to map TT to a PortMidi timestamp. The PortMidi timestamp is exactly
  MidiTime(systemTime) in ms units, and \n
     MidiTime(x) = RealToTrack(x) + PauseTime() + 1, so \n
     timestamp = TT + PauseTime() + 1 - midiLatency \n
  Note 1: The timestamp is incremented by the PortMidi stream latency
  (midiLatency) so we subtract midiLatency here for the timestamp
  passed to PortMidi. \n
  Note 2: Here, we're setting x to the time at which RealToTrack(x) = TT,
  so then MidiTime(x) is the desired timestamp. To be completely
  correct, we should assume that MidiTime(x + d) = MidiTime(x) + d,
  and consider that we compute MidiTime(systemTime) based on the
  *current* system time, but we really want the MidiTime(x) for some
  future time corresponding when MidiTime(x) = TT.)

  \par
  Also, we should assume PortMidi was opened with mMidiLatency, and that
  MIDI messages become sound with a delay of mSynthLatency. Therefore,
  the final timestamp calculation is: \n
     timestamp = TT + PauseTime() + 1 - (mMidiLatency + mSynthLatency) \n
  (All units here are seconds; some conversion is needed in the code.)

  \par
  The difference AudioTime() - PauseTime() is the time "cursor" for
  MIDI. When the speed control is used, MIDI and Audio will become
  unsynchronized. In particular, MIDI will not be synchronized with
  the visual cursor, which moves with scaled time reported in mTime.

  \par Timing in Linux
  It seems we cannot get much info from Linux. We can read the time
  when we get a callback, and we get a variable frame count (it changes
  from one callback to the next). Returning to the RealToTrack()
  equations above: \n
  RealToTrack(outputTime) = AudioTime() - PauseTime() - bufferDuration \n
  where outputTime should be PortAudio's estimate for the most recent output
  buffer, but at least on my Dell Latitude E7450, PortAudio is getting zero
  from ALSA, so we need to find a proxy for this.

  \par Estimating outputTime (Plan A, assuming double-buffered, fixed-size buffers, please skip to Plan B)
  One can expect the audio callback to happen as soon as there is room in
  the output for another block of samples, so we could just measure system
  time at the top of the callback. Then we could add the maximum delay
  buffered in the system. E.g. if there is simple double buffering and the
  callback is computing one of the buffers, the callback happens just as
  one of the buffers empties, meaning the other buffer is full, so we have
  exactly one buffer delay before the next computed sample is output.

  If computation falls behind a bit, the callback will be later, so the
  delay to play the next computed sample will be less. I think a reasonable
  way to estimate the actual output time is to assume that the computer is
  mostly keeping up and that *most* callbacks will occur immediately when
  there is space. Note that the most likely reason for the high-priority
  audio thread to fall behind is the callback itself, but the start of the
  callback should be pretty consistently keeping up.

  Also, we do not have to have a perfect estimate of the time. Suppose we
  estimate a linear mapping from sample count to system time by saying
  that the sample count maps to the system time at the most recent callback,
  and set the slope to 1% slower than real time (as if the sample clock is
  slow). Now, at each callback, if the callback seems to occur earlier than
  expected, we can adjust the mapping to be earlier. The earlier the
  callback, the more accurate it must be. On the other hand, if the callback
  is later than predicted, it must be a delayed callback (or else the
  sample clock is more than 1% slow, which is really a hardware problem.)
  How bad can this be? Assuming callbacks every 30ms (this seems to be what
  I'm observing in a default setup), you'll be a maximum of 1ms off even if
  2 out of 3 callbacks are late. This is pretty reasonable given that
  PortMIDI clock precision is 1ms. If buffers are larger and callback timing
  is more erratic, errors will be larger, but even a few ms error is
  probably OK.

  \par Estimating outputTime (Plan B, variable framesPerBuffer in callback, please skip to Plan C)
  ALSA is complicated because we get varying values of
  framesPerBuffer from callback to callback. Assume you get more frames
  when the callback is later (because there is more accumulated input to
  deliver and more more accumulated room in the output buffers). So take
  the current time and subtract the duration of the frame count in the
  current callback. This should be a time position that is relatively
  jitter free (because we estimated the lateness by frame count and
  subtracted that out). This time position intuitively represents the
  current ADC time, or if no input, the time of the tail of the output
  buffer. If we wanted DAC time, we'd have to add the total output
  buffer duration, which should be reported by PortAudio. (If PortAudio
  is wrong, we'll be systematically shifted in time by the error.)

  Since there is still bound to be jitter, we can smooth these estimates.
  First, we will assume a linear mapping from system time to audio time
  with slope = 1, so really it's just the offset we need, which is going
  to be a double that we can read/write atomically without locks or
  anything fancy. (Maybe it should be "volatile".)

  To improve the estimate, we get a new offset every callback, so we can
  create a "smooth" offset by using a simple regression model (also
  this could be seen as a first order filter). The following formula
  updates smooth_offset with a new offset estimate in the callback:
      smooth_offset = smooth_offset * 0.9 + new_offset_estimate * 0.1
  Since this is smooth, we'll have to be careful to give it a good initial
  value to avoid a long convergence.

  \par Estimating outputTime (Plan C)
  ALSA is complicated because we get varying values of
  framesPerBuffer from callback to callback. It seems there is a lot
  of variation in callback times and buffer space. One solution would
  be to go to fixed size double buffer, but Audacity seems to work
  better as is, so Plan C is to rely on one invariant which is that
  the output buffer cannot overflow, so there's a limit to how far
  ahead of the DAC time we can be writing samples into the
  buffer. Therefore, we'll assume that the audio clock runs slow by
  about 0.2% and we'll assume we're computing at that rate. If the
  actual output position is ever ahead of the computed position, we'll
  increase the computed position to the actual position. Thus whenever
  the buffer is less than near full, we'll stay ahead of DAC time,
  falling back at a rate of about 0.2% until eventually there's
  another near-full buffer callback that will push the time back ahead.

  \par Interaction between MIDI, Audio, and Pause
  When Pause is used, PauseTime() will increase at the same rate as
  AudioTime(), and no more events will be output. Because of the
  time advance of mAudioOutputLatency + MIDI_SLEEP + latency and the
  fact that
  AudioTime() advances stepwise by mAudioBufferDuration, some extra MIDI
  might be output, but the same is true of audio: something like
  mAudioOutputLatency audio samples will be in the output buffer
  (with up to mAudioBufferDuration additional samples, depending on
  when the Pause takes effect). When playback is resumed, there will
  be a slight delay corresponding to the extra data previously sent.
  Again, the same is true of audio. Audio and MIDI will not pause and
  resume at exactly the same times, but their pause and resume times
  will be within the low tens of milliseconds, and the streams will
  be synchronized in any case. I.e. if audio pauses 10ms earlier than
  MIDI, it will resume 10ms earlier as well.

  \par PortMidi Latency Parameter
  PortMidi has a "latency" parameter that is added to all timestamps.
  This value must be greater than zero to enable timestamp-based timing,
  but serves no other function, so we will set it to 1. All timestamps
  must then be adjusted down by 1 before messages are sent. This
  adjustment is on top of all the calculations described above. It just
  seem too complicated to describe everything in complete detail in one
  place.

  \par Midi with a time track
  When a variable-speed time track is present, MIDI events are output
  with the times used by the time track (rather than the raw times).
  This ensures MIDI is synchronized with audio.

  \par Midi While Recording Only or Without Audio Playback
  To reduce duplicate code and to ensure recording is synchronised with
  MIDI, a portaudio stream will always be used, even when there is no
  actual audio output.  For recording, this ensures that the recorded
  audio will by synchronized with the MIDI (otherwise, it gets out-of-
  sync if played back with correct timing).

  \par NoteTrack PlayLooped
  When mPlayLooped is true, output is supposed to loop from mT0 to mT1.
  For NoteTracks, we interpret this to mean that any note-on or control
  change in the range mT0 <= t < mT1 is sent (notes that start before
  mT0 are not played even if they extend beyond mT0). Then, all notes
  are turned off. Events in the range mT0 <= t < mT1 are then repeated,
  offset by (mT1 - mT0), etc.  We do NOT go back to the beginning and
  play all control changes (update events) up to mT0, nor do we "undo"
  any state changes between mT0 and mT1.

  \par NoteTrack PlayLooped Implementation
  The mIterator object (an Alg_iterator) returns NULL when there are
  no more events scheduled before mT1. At mT1, we want to output
  all notes off messages, but the FillMidiBuffers() loop will exit
  if mNextEvent is NULL, so we create a "fake" mNextEvent for this
  special "event" of sending all notes off. After that, we destroy
  the iterator and use PrepareMidiIterator() to set up a NEW one.
  At each iteration, time must advance by (mT1 - mT0), so the
  accumulated complete loop time (in "unwarped," track time) is computed
  by MidiLoopOffset().

  \todo run through all functions called from audio and portaudio threads
  to verify they are thread-safe. Note that synchronization of the style:
  "A sets flag to signal B, B clears flag to acknowledge completion"
  is not thread safe in a general multiple-CPU context. For example,
  B can write to a buffer and set a completion flag. The flag write can
  occur before the buffer write due to out-of-order execution. Then A
  can see the flag and read the buffer before buffer writes complete.

*//****************************************************************//**

\class AudioThread
\brief Defined different on Mac and other platforms (on Mac it does not
use wxWidgets wxThread), this class sits in a thread loop reading and
writing audio.

*//****************************************************************//**

\class AudioIOListener
\brief Monitors record play start/stop and new blockfiles.  Has 
callbacks for these events.

*//****************************************************************//**

\class AudioIOStartStreamOptions
\brief struct holding stream options, including a pointer to the 
TimeTrack and AudioIOListener and whether the playback is looped.

*//*******************************************************************/

#include "Audacity.h"
#include "Experimental.h"
#include "AudioIO.h"
#include "float_cast.h"
#include "DeviceManager.h"

#include <cfloat>
#include <math.h>
#include <stdlib.h>
#include <algorithm>

#ifdef __WXMSW__
#include <malloc.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#if USE_PORTMIXER
#include "portmixer.h"
#endif

#include <wx/log.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/intl.h>
#include <wx/debug.h>
#include <wx/sstream.h>
#include <wx/txtstrm.h>

#include "AudacityApp.h"
#include "AudacityException.h"
#include "Mix.h"
#include "MixerBoard.h"
#include "Resample.h"
#include "RingBuffer.h"
#include "prefs/GUISettings.h"
#include "Prefs.h"
#include "Project.h"
#include "TimeTrack.h"
#include "WaveTrack.h"
#include "AutoRecovery.h"

#include "prefs/QualityPrefs.h"
#include "toolbars/ControlToolBar.h"
#include "widgets/Meter.h"
#include "widgets/ErrorDialog.h"
#include "widgets/Warning.h"

#ifdef EXPERIMENTAL_MIDI_OUT
   #define MIDI_SLEEP 10 /* milliseconds */
   // how long do we think the thread that fills MIDI buffers,
   // if it is separate from the portaudio thread,
   // might be delayed due to other threads?
   #ifdef USE_MIDI_THREAD
      #define THREAD_LATENCY 10 /* milliseconds */
   #else
      #define THREAD_LATENCY 0 /* milliseconds */
   #endif
   #define ROUND(x) (int) ((x)+0.5)
   //#include <string.h>
   #include "../lib-src/portmidi/pm_common/portmidi.h"
   #include "../lib-src/portaudio-v19/src/common/pa_util.h"
   #include "NoteTrack.h"
#endif

#ifdef EXPERIMENTAL_AUTOMATED_INPUT_LEVEL_ADJUSTMENT
   #define LOWER_BOUND 0.0
   #define UPPER_BOUND 1.0
#endif

using std::max;
using std::min;

std::unique_ptr<AudioIO> ugAudioIO;
AUDACITY_DLL_API AudioIO *gAudioIO{};

wxDEFINE_EVENT(EVT_AUDIOIO_PLAYBACK, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUDIOIO_CAPTURE, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUDIOIO_MONITOR, wxCommandEvent);

// static
int AudioIO::mNextStreamToken = 0;
int AudioIO::mCachedPlaybackIndex = -1;
std::vector<long> AudioIO::mCachedPlaybackRates;
int AudioIO::mCachedCaptureIndex = -1;
std::vector<long> AudioIO::mCachedCaptureRates;
std::vector<long> AudioIO::mCachedSampleRates;
double AudioIO::mCachedBestRateIn = 0.0;
double AudioIO::mCachedBestRateOut;

enum {
   // This is the least positive latency we can
   // specify to Pm_OpenOutput, 1 ms, which prevents immediate
   // scheduling of events:
   MIDI_MINIMAL_LATENCY_MS = 1
};

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT

#include "tracks/ui/Scrubbing.h"

#ifdef __WXGTK__
   // Might #define this for a useful thing on Linux
   #undef REALTIME_ALSA_THREAD
#else
   // never on the other operating systems
   #undef REALTIME_ALSA_THREAD
#endif

#ifdef REALTIME_ALSA_THREAD
#include "pa_linux_alsa.h"
#endif


/*
This work queue class, with the aid of the playback ring
buffers, coordinates three threads during scrub play:

The UI thread which specifies scrubbing intervals to play,

The Audio thread which consumes those specifications a first time
and fills the ring buffers with samples for play,

The PortAudio thread which consumes from the ring buffers, then
also consumes a second time from this queue,
to figure out how to update mTime

-- which the UI thread, in turn, uses to redraw the play head indicator
in the right place.

Audio produces samples for PortAudio, which consumes them, both in
approximate real time.  The UI thread might go idle and so the others
might catch up, emptying the queue and causing scrub to go silent.
The UI thread will not normally outrun the others -- because InitEntry()
limits the real time duration over which each enqueued interval will play.
So a small, fixed queue size should be adequate.
*/
struct AudioIO::ScrubQueue
{
   ScrubQueue(double t0, double t1, wxLongLong startClockMillis,
              double rate, long maxDebt,
              const ScrubbingOptions &options)
      : mTrailingIdx(0)
      , mMiddleIdx(1)
      , mLeadingIdx(1)
      , mRate(rate)
      , mLastScrubTimeMillis(startClockMillis)
      , mMaxDebt { maxDebt }
      , mUpdating()
   {
      const auto s0 = std::max(options.minSample, std::min(options.maxSample,
         sampleCount(lrint(t0 * mRate))
      ));
      const auto s1 = sampleCount(lrint(t1 * mRate));
      Duration dd { *this };
      auto actualDuration = std::max(sampleCount{1}, dd.duration);
      auto success = mEntries[mMiddleIdx].Init(nullptr,
         s0, s1, actualDuration, options);
      if (success)
         ++mLeadingIdx;
      else {
         // If not, we can wait to enqueue again later
         dd.Cancel();
      }

      // So the play indicator starts out unconfused:
      {
         Entry &entry = mEntries[mTrailingIdx];
         entry.mS0 = entry.mS1 = s0;
         entry.mPlayed = entry.mDuration = 1;
      }
   }
   ~ScrubQueue() {}

   double LastTimeInQueue() const
   {
      // Needed by the main thread sometimes
      wxMutexLocker locker(mUpdating);
      const Entry &previous = mEntries[(mLeadingIdx + Size - 1) % Size];
      return previous.mS1.as_double() / mRate;
   }

   // This is for avoiding deadlocks while starting a scrub:
   // Audio stream needs to be unblocked
   void Nudge()
   {
      wxMutexLocker locker(mUpdating);
      mNudged = true;
      mAvailable.Signal();
   }

   bool Producer(double end, const ScrubbingOptions &options)
   {
      // Main thread indicates a scrubbing interval

      // MAY ADVANCE mLeadingIdx, BUT IT NEVER CATCHES UP TO mTrailingIdx.

      wxMutexLocker locker(mUpdating);
      bool result = true;
      unsigned next = (mLeadingIdx + 1) % Size;
      if (next != mTrailingIdx)
      {
         auto current = &mEntries[mLeadingIdx];
         auto previous = &mEntries[(mLeadingIdx + Size - 1) % Size];

         // Use the previous end as NEW start.
         const auto s0 = previous->mS1;
         Duration dd { *this };
         const auto &origDuration = dd.duration;
         if (origDuration <= 0)
            return false;

         auto actualDuration = origDuration;
         const sampleCount s1 ( options.enqueueBySpeed
            ? s0.as_double() +
               lrint(origDuration.as_double() * end) // end is a speed
            : lrint(end * mRate)            // end is a time
         );
         auto success =
            current->Init(previous, s0, s1, actualDuration, options);
         if (success)
            mLeadingIdx = next;
         else {
            dd.Cancel();
            return false;
         }

         // Fill up the queue with some silence if there was trimming
         wxASSERT(actualDuration <= origDuration);
         if (actualDuration < origDuration) {
            next = (mLeadingIdx + 1) % Size;
            if (next != mTrailingIdx) {
               previous = &mEntries[(mLeadingIdx + Size - 1) % Size];
               current = &mEntries[mLeadingIdx];
               current->InitSilent(*previous, origDuration - actualDuration);
               mLeadingIdx = next;
            }
            else
               // Oops, can't enqueue the silence -- so do what?
               ;
         }

         mAvailable.Signal();
         return result;
      }
      else
      {
         // ??
         // Queue wasn't long enough.  Write side (UI thread)
         // has overtaken the trailing read side (PortAudio thread), despite
         // my comments above!  We lose some work requests then.
         // wxASSERT(false);
         return false;
      }
   }

   void Transformer(sampleCount &startSample, sampleCount &endSample,
                    sampleCount &duration,
                    Maybe<wxMutexLocker> &cleanup)
   {
      // Audio thread is ready for the next interval.

      // MAY ADVANCE mMiddleIdx, WHICH MAY EQUAL mLeadingIdx, BUT DOES NOT PASS IT.

      bool checkDebt = false;
      if (!cleanup) {
         cleanup.create(mUpdating);

         // Check for cancellation of work only when re-enetering the cricial section
         checkDebt = true;
      }
      while(!mNudged && mMiddleIdx == mLeadingIdx)
         mAvailable.Wait();

      mNudged = false;

      auto now = ::wxGetLocalTimeMillis();

      if (checkDebt &&
          mLastTransformerTimeMillis >= 0 && // Not the first time for this scrub
          mMiddleIdx != mLeadingIdx) {
         // There is work in the queue, but if Producer is outrunning us, discard some,
         // which may make a skip yet keep playback better synchronized with user gestures.
         const auto interval = (now - mLastTransformerTimeMillis).ToDouble() / 1000.0;
         //const Entry &previous = mEntries[(mMiddleIdx + Size - 1) % Size];
         const auto deficit =
            static_cast<long>(interval * mRate) - // Samples needed in the last time interval
            mCredit;                              // Samples done in the last time interval
         mCredit = 0;
         mDebt += deficit;
         auto toDiscard = mDebt - mMaxDebt;
         while (toDiscard > 0 && mMiddleIdx != mLeadingIdx) {
            // Cancel some debt (discard some NEW work)
            auto &entry = mEntries[mMiddleIdx];
            auto &dur = entry.mDuration;
            if (toDiscard >= dur) {
               // Discard entire queue entry
               mDebt -= dur;
               toDiscard -= dur;
               dur = 0; // So Consumer() will handle abandoned entry correctly
               mMiddleIdx = (mMiddleIdx + 1) % Size;
            }
            else {
               // Adjust the start time
               auto &start = entry.mS0;
               const auto end = entry.mS1;
               const auto ratio = toDiscard.as_double() / dur.as_double();
               const sampleCount adjustment(
                  std::abs((end - start).as_long_long()) * ratio
               );
               if (start <= end)
                  start += adjustment;
               else
                  start -= adjustment;

               mDebt -= toDiscard;
               dur -= toDiscard;
               toDiscard = 0;
            }
         }
      }

      if (mMiddleIdx != mLeadingIdx) {
         // There is still work in the queue, after cancelling debt
         Entry &entry = mEntries[mMiddleIdx];
         startSample = entry.mS0;
         endSample = entry.mS1;
         duration = entry.mDuration;
         mMiddleIdx = (mMiddleIdx + 1) % Size;
         mCredit += duration;
      }
      else {
         // We got the shut-down signal, or we got nudged, or we discarded all the work.
         startSample = endSample = duration = -1L;
      }

      if (checkDebt)
         mLastTransformerTimeMillis = now;
   }

   double Consumer(unsigned long frames)
   {
      // Portaudio thread consumes samples and must update
      // the time for the indicator.  This finds the time value.

      // MAY ADVANCE mTrailingIdx, BUT IT NEVER CATCHES UP TO mMiddleIdx.

      wxMutexLocker locker(mUpdating);

      // Mark entries as partly or fully "consumed" for
      // purposes of mTime update.  It should not happen that
      // frames exceed the total of samples to be consumed,
      // but in that case we just use the t1 of the latest entry.
      while (1)
      {
         Entry *pEntry = &mEntries[mTrailingIdx];
         auto remaining = pEntry->mDuration - pEntry->mPlayed;
         if (frames >= remaining)
         {
            // remaining is not more than frames
            frames -= remaining.as_size_t();
            pEntry->mPlayed = pEntry->mDuration;
         }
         else
         {
            pEntry->mPlayed += frames;
            break;
         }
         const unsigned next = (mTrailingIdx + 1) % Size;
         if (next == mMiddleIdx)
            break;
         mTrailingIdx = next;
      }
      return mEntries[mTrailingIdx].GetTime(mRate);
   }

private:
   struct Entry
   {
      Entry()
         : mS0(0)
         , mS1(0)
         , mGoal(0)
         , mDuration(0)
         , mPlayed(0)
      {}

      bool Init(Entry *previous, sampleCount s0, sampleCount s1,
         sampleCount &duration /* in/out */,
         const ScrubbingOptions &options)
      {
         const bool &adjustStart = options.adjustStart;

         wxASSERT(duration > 0);
         double speed =
            (std::abs((s1 - s0).as_long_long())) / duration.as_double();
         bool adjustedSpeed = false;

         auto minSpeed = std::min(options.minSpeed, options.maxSpeed);
         wxASSERT(minSpeed == options.minSpeed);

         // May change the requested speed and duration
         if (!adjustStart && speed > options.maxSpeed)
         {
            // Reduce speed to the maximum selected in the user interface.
            speed = options.maxSpeed;
            mGoal = s1;
            adjustedSpeed = true;
         }
         else if (!adjustStart &&
            previous &&
            previous->mGoal >= 0 &&
            previous->mGoal == s1)
         {
            // In case the mouse has not moved, and playback
            // is catching up to the mouse at maximum speed,
            // continue at no less than maximum.  (Without this
            // the final catch-up can make a slow scrub interval
            // that drops the pitch and sounds wrong.)
            minSpeed = options.maxSpeed;
            mGoal = s1;
            adjustedSpeed = true;
         }
         else
            mGoal = -1;

         if (speed < minSpeed) {
            // Trim the duration.
            duration = std::max(0L, lrint(speed * duration.as_double() / minSpeed));
            speed = minSpeed;
            adjustedSpeed = true;
         }

         if (speed < ScrubbingOptions::MinAllowedScrubSpeed()) {
            // Mixers were set up to go only so slowly, not slower.
            // This will put a request for some silence in the work queue.
            adjustedSpeed = true;
            speed = 0.0;
         }

         // May change s1 or s0 to match speed change or stay in bounds of the project

         if (adjustedSpeed && !adjustStart)
         {
            // adjust s1
            const sampleCount diff = lrint(speed * duration.as_double());
            if (s0 < s1)
               s1 = s0 + diff;
            else
               s1 = s0 - diff;
         }

         bool silent = false;

         // Adjust s1 (again), and duration, if s1 is out of bounds,
         // or abandon if a stutter is too short.
         // (Assume s0 is in bounds, because it equals the last scrub's s1 which was checked.)
         if (s1 != s0)
         {
            auto newDuration = duration;
            const auto newS1 = std::max(options.minSample, std::min(options.maxSample, s1));
            if(s1 != newS1)
               newDuration = std::max( sampleCount{ 0 },
                  sampleCount(
                     duration.as_double() * (newS1 - s0).as_double() /
                        (s1 - s0).as_double()
                  )
               );
            // When playback follows a fast mouse movement by "stuttering"
            // at maximum playback, don't make stutters too short to be useful.
            if (options.adjustStart && newDuration < options.minStutter)
               return false;
            else if (newDuration == 0) {
               // Enqueue a silent scrub with s0 == s1
               silent = true;
               s1 = s0;
            }
            else if (s1 != newS1) {
               // Shorten
               duration = newDuration;
               s1 = newS1;
            }
         }

         if (adjustStart && !silent)
         {
            // Limit diff because this is seeking.
            const sampleCount diff =
               lrint(std::min(options.maxSpeed, speed) * duration.as_double());
            if (s0 < s1)
               s0 = s1 - diff;
            else
               s0 = s1 + diff;
         }

         mS0 = s0;
         mS1 = s1;
         mPlayed = 0;
         mDuration = duration;
         return true;
      }

      void InitSilent(const Entry &previous, sampleCount duration)
      {
         mGoal = previous.mGoal;
         mS0 = mS1 = previous.mS1;
         mPlayed = 0;
         mDuration = duration;
      }

      double GetTime(double rate) const
      {
         return
            (mS0.as_double() +
             (mS1 - mS0).as_double() * mPlayed.as_double() / mDuration.as_double())
            / rate;
      }

      // These sample counts are initialized in the UI, producer, thread:
      sampleCount mS0;
      sampleCount mS1;
      sampleCount mGoal;
      // This field is initialized in the UI thread too, and
      // this work queue item corresponds to exactly this many samples of
      // playback output:
      sampleCount mDuration;

      // The middleman Audio thread does not change these entries, but only
      // changes indices in the queue structure.

      // This increases from 0 to mDuration as the PortAudio, consumer,
      // thread catches up.  When they are equal, this entry can be discarded:
      sampleCount mPlayed;
   };

   struct Duration {
      Duration (ScrubQueue &queue_) : queue(queue_) {}
      ~Duration ()
      {
         if(!cancelled)
            queue.mLastScrubTimeMillis = clockTime;
      }

      void Cancel() { cancelled = true; }

      ScrubQueue &queue;
      const wxLongLong clockTime { ::wxGetLocalTimeMillis() };
      const sampleCount duration { static_cast<long long>
         (queue.mRate * (clockTime - queue.mLastScrubTimeMillis).ToDouble() / 1000.0)
      };
      bool cancelled { false };
   };

   enum { Size = 10 };
   Entry mEntries[Size];
   unsigned mTrailingIdx;
   unsigned mMiddleIdx;
   unsigned mLeadingIdx;
   const double mRate;
   wxLongLong mLastScrubTimeMillis;

   wxLongLong mLastTransformerTimeMillis { -1LL };
   sampleCount mCredit { 0 };
   sampleCount mDebt { 0 };
   const long mMaxDebt;

   mutable wxMutex mUpdating;
   mutable wxCondition mAvailable { mUpdating };
   bool mNudged { false };
};
#endif

#ifdef EXPERIMENTAL_MIDI_OUT
// return the system time as a double
static double streamStartTime = 0; // bias system time to small number

static double SystemTime(bool usingAlsa)
{
#ifdef __WXGTK__
   if (usingAlsa) {
      struct timespec now;
      // CLOCK_MONOTONIC_RAW is unaffected by NTP or adj-time
      clock_gettime(CLOCK_MONOTONIC_RAW, &now);
      //return now.tv_sec + now.tv_nsec * 0.000000001;
      return (now.tv_sec + now.tv_nsec * 0.000000001) - streamStartTime;
   }
#else
   static_cast<void>(usingAlsa);//compiler food.
#endif

   return PaUtil_GetTime() - streamStartTime;
}
#endif

const int AudioIO::StandardRates[] = {
   8000,
   11025,
   16000,
   22050,
   32000,
   44100,
   48000,
   88200,
   96000,
   176400,
   192000,
   352800,
   384000
};
const int AudioIO::NumStandardRates = sizeof(AudioIO::StandardRates) /
                                      sizeof(AudioIO::StandardRates[0]);
const int AudioIO::RatesToTry[] = {
   8000,
   9600,
   11025,
   12000,
   15000,
   16000,
   22050,
   24000,
   32000,
   44100,
   48000,
   88200,
   96000,
   176400,
   192000,
   352800,
   384000
};
const int AudioIO::NumRatesToTry = sizeof(AudioIO::RatesToTry) /
                                      sizeof(AudioIO::RatesToTry[0]);

int audacityAudioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags, void *userData );

//////////////////////////////////////////////////////////////////////
//
//     class AudioThread - declaration and glue code
//
//////////////////////////////////////////////////////////////////////

#ifdef __WXMAC__

// On Mac OS X, it's better not to use the wxThread class.
// We use our own implementation based on pthreads instead.

#include <pthread.h>
#include <time.h>

class AudioThread {
 public:
   typedef int ExitCode;
   AudioThread() { mDestroy = false; mThread = NULL; }
   virtual ExitCode Entry();
   void Create() {}
   void Delete() {
      mDestroy = true;
      pthread_join(mThread, NULL);
   }
   bool TestDestroy() { return mDestroy; }
   void Sleep(int ms) {
      struct timespec spec;
      spec.tv_sec = 0;
      spec.tv_nsec = ms * 1000 * 1000;
      nanosleep(&spec, NULL);
   }
   static void *callback(void *p) {
      AudioThread *th = (AudioThread *)p;
      return (void *)th->Entry();
   }
   void Run() {
      pthread_create(&mThread, NULL, callback, this);
   }
 private:
   bool mDestroy;
   pthread_t mThread;

};

#else

// The normal wxThread-derived AudioThread class for all other
// platforms:
class AudioThread /* not final */ : public wxThread {
 public:
   AudioThread():wxThread(wxTHREAD_JOINABLE) {}
   ExitCode Entry() override;
};

#endif

#ifdef EXPERIMENTAL_MIDI_OUT
class MidiThread final : public AudioThread {
 public:
   ExitCode Entry() override;
};
#endif


//////////////////////////////////////////////////////////////////////
//
//     UI Thread Context
//
//////////////////////////////////////////////////////////////////////

void InitAudioIO()
{
   ugAudioIO.reset(safenew AudioIO());
   gAudioIO = ugAudioIO.get();
   gAudioIO->mThread->Run();
#ifdef EXPERIMENTAL_MIDI_OUT
#ifdef USE_MIDI_THREAD
   gAudioIO->mMidiThread->Run();
#endif
#endif

   // Make sure device prefs are initialized
   if (gPrefs->Read(wxT("AudioIO/RecordingDevice"), wxT("")) == wxT("")) {
      int i = AudioIO::getRecordDevIndex();
      const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
      if (info) {
         gPrefs->Write(wxT("/AudioIO/RecordingDevice"), DeviceName(info));
         gPrefs->Write(wxT("/AudioIO/Host"), HostName(info));
      }
   }

   if (gPrefs->Read(wxT("AudioIO/PlaybackDevice"), wxT("")) == wxT("")) {
      int i = AudioIO::getPlayDevIndex();
      const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
      if (info) {
         gPrefs->Write(wxT("/AudioIO/PlaybackDevice"), DeviceName(info));
         gPrefs->Write(wxT("/AudioIO/Host"), HostName(info));
      }
   }

   gPrefs->Flush();
}

void DeinitAudioIO()
{
   ugAudioIO.reset();
}

wxString DeviceName(const PaDeviceInfo* info)
{
   wxString infoName = wxSafeConvertMB2WX(info->name);

   return infoName;
}

wxString HostName(const PaDeviceInfo* info)
{
   wxString hostapiName = wxSafeConvertMB2WX(Pa_GetHostApiInfo(info->hostApi)->name);

   return hostapiName;
}

bool AudioIO::ValidateDeviceNames(const wxString &play, const wxString &rec)
{
   const PaDeviceInfo *pInfo = Pa_GetDeviceInfo(AudioIO::getPlayDevIndex(play));
   const PaDeviceInfo *rInfo = Pa_GetDeviceInfo(AudioIO::getRecordDevIndex(rec));

   if (!pInfo || !rInfo || pInfo->hostApi != rInfo->hostApi) {
      return false;
   }

   return true;
}

AudioIO::AudioIO()
{
   if (!std::atomic<double>{}.is_lock_free()) {
      // If this check fails, then the atomic<double> members in AudioIO.h
      // might be changed to atomic<float> to be more efficient with some
      // loss of precision.  That could be conditionally compiled depending
      // on the platform.
      wxASSERT(false);
   }

   mAudioThreadShouldCallFillBuffersOnce = false;
   mAudioThreadFillBuffersLoopRunning = false;
   mAudioThreadFillBuffersLoopActive = false;
   mPortStreamV19 = NULL;

#ifdef EXPERIMENTAL_MIDI_OUT
   mMidiStream = NULL;
   mMidiThreadFillBuffersLoopRunning = false;
   mMidiThreadFillBuffersLoopActive = false;
   mMidiStreamActive = false;
   mSendMidiState = false;
   mIterator = NULL;

   mNumFrames = 0;
   mNumPauseFrames = 0;
#endif

#ifdef EXPERIMENTAL_AUTOMATED_INPUT_LEVEL_ADJUSTMENT
   mAILAActive = false;
#endif
   mStreamToken = 0;

   mLastPaError = paNoError;

   mLastRecordingOffset = 0.0;
   mNumCaptureChannels = 0;
   mPaused = false;

   mListener = NULL;
   mUpdateMeters = false;
   mUpdatingMeters = false;

   mOwningProject = NULL;
   mOutputMeter = NULL;

   PaError err = Pa_Initialize();

   if (err != paNoError) {
      wxString errStr = _("Could not find any audio devices.\n");
      errStr += _("You will not be able to play or record audio.\n\n");
      wxString paErrStr = LAT1CTOWX(Pa_GetErrorText(err));
      if (!paErrStr.IsEmpty())
         errStr += _("Error: ")+paErrStr;
      // XXX: we are in libaudacity, popping up dialogs not allowed!  A
      // long-term solution will probably involve exceptions
      AudacityMessageBox(errStr, _("Error Initializing Audio"), wxICON_ERROR|wxOK);

      // Since PortAudio is not initialized, all calls to PortAudio
      // functions will fail.  This will give reasonable behavior, since
      // the user will be able to do things not relating to audio i/o,
      // but any attempt to play or record will simply fail.
   }

#ifdef EXPERIMENTAL_MIDI_OUT
   PmError pmErr = Pm_Initialize();

   if (pmErr != pmNoError) {
      wxString errStr =
              _("There was an error initializing the midi i/o layer.\n");
      errStr += _("You will not be able to play midi.\n\n");
      wxString pmErrStr = LAT1CTOWX(Pm_GetErrorText(pmErr));
      if (!pmErrStr.empty())
         errStr += _("Error: ") + pmErrStr;
      // XXX: we are in libaudacity, popping up dialogs not allowed!  A
      // long-term solution will probably involve exceptions
      AudacityMessageBox(errStr, _("Error Initializing Midi"), wxICON_ERROR|wxOK);

      // Same logic for PortMidi as described above for PortAudio
   }

#ifdef USE_MIDI_THREAD
   mMidiThread = std::make_unique<MidiThread>();
   mMidiThread->Create();
#endif

#endif

   // Start thread
   mThread = std::make_unique<AudioThread>();
   mThread->Create();

#if defined(USE_PORTMIXER)
   mPortMixer = NULL;
   mPreviousHWPlaythrough = -1.0;
   HandleDeviceChange();
#else
   mEmulateMixerOutputVol = true;
   mMixerOutputVol = 1.0;
   mInputMixerWorks = false;
#endif

   mLastPlaybackTimeMillis = 0;

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   mScrubQueue = NULL;
   mScrubDuration = 0;
   mSilentScrub = false;
#endif
}

AudioIO::~AudioIO()
{
#if defined(USE_PORTMIXER)
   if (mPortMixer) {
      #if __WXMAC__
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
      Px_CloseMixer(mPortMixer);
      mPortMixer = NULL;
   }
#endif

   // FIXME: ? TRAP_ERR.  Pa_Terminate probably OK if err without reporting.
   Pa_Terminate();

#ifdef EXPERIMENTAL_MIDI_OUT
   Pm_Terminate();

   /* Delete is a "graceful" way to stop the thread.
   (Kill is the not-graceful way.) */

#ifdef USE_MIDI_THREAD
   mMidiThread->Delete();
   mMidiThread.reset();
#endif

#endif

   /* Delete is a "graceful" way to stop the thread.
      (Kill is the not-graceful way.) */

   // This causes reentrancy issues during application shutdown
   // wxTheApp->Yield();

   mThread->Delete();
   mThread.reset();

   gAudioIO = nullptr;
}

void AudioIO::SetMixer(int inputSource)
{
#if defined(USE_PORTMIXER)
   int oldRecordSource = Px_GetCurrentInputSource(mPortMixer);
   if ( inputSource != oldRecordSource )
         Px_SetCurrentInputSource(mPortMixer, inputSource);
#endif
}
void AudioIO::SetMixer(int inputSource, float recordVolume,
                       float playbackVolume)
{
   mMixerOutputVol = playbackVolume;

#if defined(USE_PORTMIXER)
   PxMixer *mixer = mPortMixer;

   if( mixer )
   {
      float oldRecordVolume = Px_GetInputVolume(mixer);
      float oldPlaybackVolume = Px_GetPCMOutputVolume(mixer);

      SetMixer(inputSource);
      if( oldRecordVolume != recordVolume )
         Px_SetInputVolume(mixer, recordVolume);
      if( oldPlaybackVolume != playbackVolume )
         Px_SetPCMOutputVolume(mixer, playbackVolume);

      return;
   }
#endif
}

void AudioIO::GetMixer(int *recordDevice, float *recordVolume,
                       float *playbackVolume)
{
#if defined(USE_PORTMIXER)

   PxMixer *mixer = mPortMixer;

   if( mixer )
   {
      *recordDevice = Px_GetCurrentInputSource(mixer);

      if (mInputMixerWorks)
         *recordVolume = Px_GetInputVolume(mixer);
      else
         *recordVolume = 1.0f;

      if (mEmulateMixerOutputVol)
         *playbackVolume = mMixerOutputVol;
      else
         *playbackVolume = Px_GetPCMOutputVolume(mixer);

      return;
   }

#endif

   *recordDevice = 0;
   *recordVolume = 1.0f;
   *playbackVolume = mMixerOutputVol;
}

bool AudioIO::InputMixerWorks()
{
   return mInputMixerWorks;
}

bool AudioIO::OutputMixerEmulated()
{
   return mEmulateMixerOutputVol;
}

wxArrayString AudioIO::GetInputSourceNames()
{
#if defined(USE_PORTMIXER)

   wxArrayString deviceNames;

   if( mPortMixer )
   {
      int numSources = Px_GetNumInputSources(mPortMixer);
      for( int source = 0; source < numSources; source++ )
         deviceNames.Add(wxString(wxSafeConvertMB2WX(Px_GetInputSourceName(mPortMixer, source))));
   }
   else
   {
      wxLogDebug(wxT("AudioIO::GetInputSourceNames(): PortMixer not initialised!"));
   }

   return deviceNames;

#else

   wxArrayString blank;

   return blank;

#endif
}

void AudioIO::HandleDeviceChange()
{
   // This should not happen, but it would screw things up if it did.
   // Vaughan, 2010-10-08: But it *did* happen, due to a bug, and nobody
   // caught it because this method just returned. Added wxASSERT().
   wxASSERT(!IsStreamActive());
   if (IsStreamActive())
      return;

   // get the selected record and playback devices
   const int playDeviceNum = getPlayDevIndex();
   const int recDeviceNum = getRecordDevIndex();

   // If no change needed, return
   if (mCachedPlaybackIndex == playDeviceNum &&
       mCachedCaptureIndex == recDeviceNum)
       return;

   // cache playback/capture rates
   mCachedPlaybackRates = GetSupportedPlaybackRates(playDeviceNum);
   mCachedCaptureRates = GetSupportedCaptureRates(recDeviceNum);
   mCachedSampleRates = GetSupportedSampleRates(playDeviceNum, recDeviceNum);
   mCachedPlaybackIndex = playDeviceNum;
   mCachedCaptureIndex = recDeviceNum;
   mCachedBestRateIn = 0.0;

#if defined(USE_PORTMIXER)

   // if we have a PortMixer object, close it down
   if (mPortMixer) {
      #if __WXMAC__
      // on the Mac we must make sure that we restore the hardware playthrough
      // state of the sound device to what it was before, because there isn't
      // a UI for this (!)
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
      Px_CloseMixer(mPortMixer);
      mPortMixer = NULL;
   }

   // that might have given us no rates whatsoever, so we have to guess an
   // answer to do the next bit
   int numrates = mCachedSampleRates.size();
   int highestSampleRate;
   if (numrates > 0)
   {
      highestSampleRate = mCachedSampleRates[numrates - 1];
   }
   else
   {  // we don't actually have any rates that work for Rec and Play. Guess one
      // to use for messing with the mixer, which doesn't actually do either
      highestSampleRate = 44100;
      // mCachedSampleRates is still empty, but it's not used again, so
      // can ignore
   }
   mInputMixerWorks = false;
   mEmulateMixerOutputVol = true;
   mMixerOutputVol = 1.0;

   int error;
   // This tries to open the device with the samplerate worked out above, which
   // will be the highest available for play and record on the device, or
   // 44.1kHz if the info cannot be fetched.

   PaStream *stream;

   PaStreamParameters playbackParameters;

   playbackParameters.device = playDeviceNum;
   playbackParameters.sampleFormat = paFloat32;
   playbackParameters.hostApiSpecificStreamInfo = NULL;
   playbackParameters.channelCount = 1;
   if (Pa_GetDeviceInfo(playDeviceNum))
      playbackParameters.suggestedLatency =
         Pa_GetDeviceInfo(playDeviceNum)->defaultLowOutputLatency;
   else
      playbackParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;

   PaStreamParameters captureParameters;

   captureParameters.device = recDeviceNum;
   captureParameters.sampleFormat = paFloat32;;
   captureParameters.hostApiSpecificStreamInfo = NULL;
   captureParameters.channelCount = 1;
   if (Pa_GetDeviceInfo(recDeviceNum))
      captureParameters.suggestedLatency =
         Pa_GetDeviceInfo(recDeviceNum)->defaultLowInputLatency;
   else
      captureParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;

   // try opening for record and playback
   error = Pa_OpenStream(&stream,
                         &captureParameters, &playbackParameters,
                         highestSampleRate, paFramesPerBufferUnspecified,
                         paClipOff | paDitherOff,
                         audacityAudioCallback, NULL);

   if (!error) {
      // Try portmixer for this stream
      mPortMixer = Px_OpenMixer(stream, 0);
      if (!mPortMixer) {
         Pa_CloseStream(stream);
         error = true;
      }
   }

   // if that failed, try just for record
   if( error ) {
      error = Pa_OpenStream(&stream,
                            &captureParameters, NULL,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);

      if (!error) {
         mPortMixer = Px_OpenMixer(stream, 0);
         if (!mPortMixer) {
            Pa_CloseStream(stream);
            error = true;
         }
      }
   }

   // finally, try just for playback
   if ( error ) {
      error = Pa_OpenStream(&stream,
                            NULL, &playbackParameters,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);

      if (!error) {
         mPortMixer = Px_OpenMixer(stream, 0);
         if (!mPortMixer) {
            Pa_CloseStream(stream);
            error = true;
         }
      }
   }

   // FIXME: TRAP_ERR errors in HandleDeviceChange not reported.
   // if it's still not working, give up
   if( error )
      return;

   // Set input source
#if USE_PORTMIXER
   int sourceIndex;
   if (gPrefs->Read(wxT("/AudioIO/RecordingSourceIndex"), &sourceIndex)) {
      if (sourceIndex >= 0) {
         //the current index of our source may be different because the stream
         //is a combination of two devices, so update it.
         sourceIndex = getRecordSourceIndex(mPortMixer);
         if (sourceIndex >= 0)
            SetMixer(sourceIndex);
      }
   }
#endif

   // Determine mixer capabilities - if it doesn't support control of output
   // signal level, we emulate it (by multiplying this value by all outgoing
   // samples)

   mMixerOutputVol = Px_GetPCMOutputVolume(mPortMixer);
   mEmulateMixerOutputVol = false;
   Px_SetPCMOutputVolume(mPortMixer, 0.0);
   if (Px_GetPCMOutputVolume(mPortMixer) > 0.1)
      mEmulateMixerOutputVol = true;
   Px_SetPCMOutputVolume(mPortMixer, 0.2f);
   if (Px_GetPCMOutputVolume(mPortMixer) < 0.1 ||
       Px_GetPCMOutputVolume(mPortMixer) > 0.3)
      mEmulateMixerOutputVol = true;
   Px_SetPCMOutputVolume(mPortMixer, mMixerOutputVol);

   float inputVol = Px_GetInputVolume(mPortMixer);
   mInputMixerWorks = true;   // assume it works unless proved wrong
   Px_SetInputVolume(mPortMixer, 0.0);
   if (Px_GetInputVolume(mPortMixer) > 0.1)
      mInputMixerWorks = false;  // can't set to zero
   Px_SetInputVolume(mPortMixer, 0.2f);
   if (Px_GetInputVolume(mPortMixer) < 0.1 ||
       Px_GetInputVolume(mPortMixer) > 0.3)
      mInputMixerWorks = false;  // can't set level accurately
   Px_SetInputVolume(mPortMixer, inputVol);

   Pa_CloseStream(stream);


   #if 0
   wxPrintf("PortMixer: Playback: %s Recording: %s\n",
          mEmulateMixerOutputVol? "emulated": "native",
          mInputMixerWorks? "hardware": "no control");
   #endif

   mMixerOutputVol = 1.0;

#endif   // USE_PORTMIXER
}

static PaSampleFormat AudacityToPortAudioSampleFormat(sampleFormat format)
{
   switch(format) {
   case int16Sample:
      return paInt16;
   case int24Sample:
      return paInt24;
   case floatSample:
   default:
      return paFloat32;
   }
}

bool AudioIO::StartPortAudioStream(double sampleRate,
                                   unsigned int numPlaybackChannels,
                                   unsigned int numCaptureChannels,
                                   sampleFormat captureFormat)
{
#ifdef EXPERIMENTAL_MIDI_OUT
   mNumFrames = 0;
   mNumPauseFrames = 0;
   // we want this initial value to be way high. It should be
   // sufficient to assume AudioTime is zero and therefore
   // mSystemMinusAudioTime is SystemTime(), but we'll add 1000s
   // for good measure. On the first callback, this should be
   // reduced to SystemTime() - mT0, and note that mT0 is always
   // positive.
   mSystemMinusAudioTimePlusLatency =
      mSystemMinusAudioTime = SystemTime(mUsingAlsa) + 1000;
   mAudioOutLatency = 0.0; // set when stream is opened
   mCallbackCount = 0;
   mAudioFramesPerBuffer = 0;
#endif
   mOwningProject = GetActiveProject();

   // PRL:  Protection from crash reported by David Bailes, involving starting
   // and stopping with frequent changes of active window, hard to reproduce
   if (!mOwningProject)
      return false;

   mInputMeter.Release();
   mOutputMeter = NULL;

   mLastPaError = paNoError;
   // pick a rate to do the audio I/O at, from those available. The project
   // rate is suggested, but we may get something else if it isn't supported
   mRate = GetBestRate(numCaptureChannels > 0, numPlaybackChannels > 0, sampleRate);

   // July 2016 (Carsten and Uwe)
   // BUG 193: Tell PortAudio sound card will handle 24 bit (under DirectSound) using 
   // userData.
   int captureFormat_saved = captureFormat;
   // Special case: Our 24-bit sample format is different from PortAudio's
   // 3-byte packed format. So just make PortAudio return float samples,
   // since we need float values anyway to apply the gain.
   // ANSWER-ME: So we *never* actually handle 24-bit?! This causes mCapture to 
   // be set to floatSample below.
   // JKC: YES that's right.  Internally Audacity uses float, and float has space for
   // 24 bits as well as exponent.  Actual 24 bit would require packing and
   // unpacking unaligned bytes and would be inefficient.
   // ANSWER ME: is floatSample 64 bit on 64 bit machines?
   if (captureFormat == int24Sample)
      captureFormat = floatSample;

   mNumPlaybackChannels = numPlaybackChannels;
   mNumCaptureChannels = numCaptureChannels;

   bool usePlayback = false, useCapture = false;
   PaStreamParameters playbackParameters{};
   PaStreamParameters captureParameters{};

   double latencyDuration = DEFAULT_LATENCY_DURATION;
   gPrefs->Read(wxT("/AudioIO/LatencyDuration"), &latencyDuration);

   if( numPlaybackChannels > 0)
   {
      usePlayback = true;

      // this sets the device index to whatever is "right" based on preferences,
      // then defaults
      playbackParameters.device = getPlayDevIndex();

      const PaDeviceInfo *playbackDeviceInfo;
      playbackDeviceInfo = Pa_GetDeviceInfo( playbackParameters.device );

      if( playbackDeviceInfo == NULL )
         return false;

      // regardless of source formats, we always mix to float
      playbackParameters.sampleFormat = paFloat32;
      playbackParameters.hostApiSpecificStreamInfo = NULL;
      playbackParameters.channelCount = mNumPlaybackChannels;

      if (mSoftwarePlaythrough)
         playbackParameters.suggestedLatency =
            playbackDeviceInfo->defaultLowOutputLatency;
      else
         playbackParameters.suggestedLatency = latencyDuration/1000.0;

      mOutputMeter = mOwningProject->GetPlaybackMeter();
   }

   if( numCaptureChannels > 0)
   {
      useCapture = true;
      mCaptureFormat = captureFormat;

      const PaDeviceInfo *captureDeviceInfo;
      // retrieve the index of the device set in the prefs, or a sensible
      // default if it isn't set/valid
      captureParameters.device = getRecordDevIndex();

      captureDeviceInfo = Pa_GetDeviceInfo( captureParameters.device );

      if( captureDeviceInfo == NULL )
         return false;

      captureParameters.sampleFormat =
         AudacityToPortAudioSampleFormat(mCaptureFormat);

      captureParameters.hostApiSpecificStreamInfo = NULL;
      captureParameters.channelCount = mNumCaptureChannels;

      if (mSoftwarePlaythrough)
         captureParameters.suggestedLatency =
            captureDeviceInfo->defaultHighInputLatency;
      else
         captureParameters.suggestedLatency = latencyDuration/1000.0;

      SetCaptureMeter( mOwningProject, mOwningProject->GetCaptureMeter() );
   }

   SetMeters();

#ifdef USE_PORTMIXER
#ifdef __WXMSW__
   //mchinen nov 30 2010.  For some reason Pa_OpenStream resets the input volume on windows.
   //so cache and restore after it.
   //The actual problem is likely in portaudio's pa_win_wmme.c OpenStream().
   float oldRecordVolume = Px_GetInputVolume(mPortMixer);
#endif
#endif

   // July 2016 (Carsten and Uwe)
   // BUG 193: Possibly tell portAudio to use 24 bit with DirectSound. 
   int  userData = 24;
   int* lpUserData = (captureFormat_saved == int24Sample) ? &userData : NULL;

   // (Linux, bug 1885) After scanning devices it takes a little time for the
   // ALSA device to be available, so allow retries.
   // On my test machine, no more than 3 attempts are required.
   unsigned int maxTries = 1;
#ifdef __WXGTK__
   if (DeviceManager::Instance()->GetTimeSinceRescan() < 10)
      maxTries = 5;
#endif

   for (unsigned int tries = 0; tries < maxTries; tries++) {
      mLastPaError = Pa_OpenStream( &mPortStreamV19,
                                    useCapture ? &captureParameters : NULL,
                                    usePlayback ? &playbackParameters : NULL,
                                    mRate, paFramesPerBufferUnspecified,
                                    paNoFlag,
                                    audacityAudioCallback, lpUserData );
      if (mLastPaError == paNoError) {
         break;
      }
      wxLogDebug("Attempt %u to open capture stream failed with: %d", 1 + tries, mLastPaError);
      wxMilliSleep(1000);
   }


#if USE_PORTMIXER
#ifdef __WXMSW__
   Px_SetInputVolume(mPortMixer, oldRecordVolume);
#endif
   if (mPortStreamV19 != NULL && mLastPaError == paNoError) {

      #ifdef __WXMAC__
      if (mPortMixer) {
         if (Px_SupportsPlaythrough(mPortMixer)) {
            bool playthrough = false;

            mPreviousHWPlaythrough = Px_GetPlaythrough(mPortMixer);

            // Bug 388.  Feature not supported.
            //gPrefs->Read(wxT("/AudioIO/Playthrough"), &playthrough, false);
            if (playthrough)
               Px_SetPlaythrough(mPortMixer, 1.0);
            else
               Px_SetPlaythrough(mPortMixer, 0.0);
         }
      }
      #endif
   }
#endif

#ifdef EXPERIMENTAL_MIDI_OUT
   // We use audio latency to estimate how far ahead of DACS we are writing
   if (mPortStreamV19 != NULL && mLastPaError == paNoError) {
      const PaStreamInfo* info = Pa_GetStreamInfo(mPortStreamV19);
      // this is an initial guess, but for PA/Linux/ALSA it's wrong and will be
      // updated with a better value:
      mAudioOutLatency = info->outputLatency;
      mSystemMinusAudioTimePlusLatency += mAudioOutLatency;
   }
#endif

   return (mLastPaError == paNoError);
}

wxString AudioIO::LastPaErrorString()
{
   return wxString::Format(wxT("%d %s."), (int) mLastPaError, Pa_GetErrorText(mLastPaError));
}

void AudioIO::StartMonitoring(double sampleRate)
{
   if ( mPortStreamV19 || mStreamToken )
      return;

   bool success;
   long captureChannels;
   auto captureFormat = QualityPrefs::SampleFormatChoice();
   gPrefs->Read(wxT("/AudioIO/RecordChannels"), &captureChannels, 2L);
   gPrefs->Read(wxT("/AudioIO/SWPlaythrough"), &mSoftwarePlaythrough, false);
   int playbackChannels = 0;

   if (mSoftwarePlaythrough)
      playbackChannels = 2;

   // FIXME: TRAP_ERR StartPortAudioStream (a PaError may be present)
   // but StartPortAudioStream function only returns true or false.
   mUsingAlsa = false;
   success = StartPortAudioStream(sampleRate, (unsigned int)playbackChannels,
                                  (unsigned int)captureChannels,
                                  captureFormat);

   if (!success) {
      wxString msg = wxString::Format(_("Error opening recording device.\nError code: %s"), gAudioIO->LastPaErrorString());
      ShowErrorDialog(mOwningProject, _("Error"), msg, wxT("Error_opening_sound_device"));
      return;
   }

   wxCommandEvent e(EVT_AUDIOIO_MONITOR);
   e.SetEventObject(mOwningProject);
   e.SetInt(true);
   wxTheApp->ProcessEvent(e);

   // FIXME: TRAP_ERR PaErrorCode 'noted' but not reported in StartMonitoring.
   // Now start the PortAudio stream!
   // TODO: ? Factor out and reuse error reporting code from end of 
   // AudioIO::StartStream?
   mLastPaError = Pa_StartStream( mPortStreamV19 );

   // Update UI display only now, after all possibilities for error are past.
   if ((mLastPaError == paNoError) && mListener) {
      // advertise the chosen I/O sample rate to the UI
      mListener->OnAudioIORate((int)mRate);
   }
}

int AudioIO::StartStream(const TransportTracks &tracks,
                         double t0, double t1,
                         const AudioIOStartStreamOptions &options)
{
   mLostSamples = 0;
   mLostCaptureIntervals.clear();
   mDetectDropouts =
      gPrefs->Read( WarningDialogKey(wxT("DropoutDetected")), true ) != 0;
   auto cleanup = finally ( [this] { ClearRecordingException(); } );

   if( IsBusy() )
      return 0;

   const auto &sampleRate = options.rate;

   // We just want to set mStreamToken to -1 - this way avoids
   // an extremely rare but possible race condition, if two functions
   // somehow called StartStream at the same time...
   mStreamToken--;
   if (mStreamToken != -1)
      return 0;

   // TODO: we don't really need to close and reopen stream if the
   // format matches; however it's kind of tricky to keep it open...
   //
   //   if (sampleRate == mRate &&
   //       playbackChannels == mNumPlaybackChannels &&
   //       captureChannels == mNumCaptureChannels &&
   //       captureFormat == mCaptureFormat) {

   if (mPortStreamV19) {
      StopStream();
      while(mPortStreamV19)
         wxMilliSleep( 50 );
   }

#ifdef __WXGTK__
   // Detect whether ALSA is the chosen host, and do the various involved MIDI
   // timing compensations only then.
   mUsingAlsa = (gPrefs->Read(wxT("/AudioIO/Host"), wxT("")) == "ALSA");
#endif

   gPrefs->Read(wxT("/AudioIO/SWPlaythrough"), &mSoftwarePlaythrough, false);
   gPrefs->Read(wxT("/AudioIO/SoundActivatedRecord"), &mPauseRec, false);
   int silenceLevelDB;
   gPrefs->Read(wxT("/AudioIO/SilenceLevel"), &silenceLevelDB, -50);
   int dBRange;
   dBRange = gPrefs->Read(ENV_DB_KEY, ENV_DB_RANGE);
   if(silenceLevelDB < -dBRange)
   {
      silenceLevelDB = -dBRange + 3;   // meter range was made smaller than SilenceLevel
      gPrefs->Write(ENV_DB_KEY, dBRange); // so set SilenceLevel reasonable
      gPrefs->Flush();
   }
   mSilenceLevel = (silenceLevelDB + dBRange)/(double)dBRange;  // meter goes -dBRange dB -> 0dB

   // Clamp pre-roll so we don't play before time 0
   const auto preRoll = std::max(0.0, std::min(t0, options.preRoll));
   mRecordingSchedule = {};
   mRecordingSchedule.mPreRoll = preRoll;
   mRecordingSchedule.mLatencyCorrection =
      (gPrefs->ReadDouble(wxT("/AudioIO/LatencyCorrection"),
                   DEFAULT_LATENCY_CORRECTION))
         / 1000.0;
   mRecordingSchedule.mDuration = t1 - t0;
   if (options.pCrossfadeData)
      mRecordingSchedule.mCrossfadeData.swap( *options.pCrossfadeData );

   mListener = options.listener;
   mRate    = sampleRate;

   mSeek    = 0;
   mLastRecordingOffset = 0;
   mCaptureTracks = tracks.captureTracks;
   mPlaybackTracks = tracks.playbackTracks;
#ifdef EXPERIMENTAL_MIDI_OUT
   mMidiPlaybackTracks = tracks.midiTracks;
#endif

   bool commit = false;
   auto cleanupTracks = finally([&]{
      if (!commit) {
         // Don't keep unnecessary shared pointers to tracks
         mPlaybackTracks.clear();
         mCaptureTracks.clear();
#ifdef EXPERIMENTAL_MIDI_OUT
         mMidiPlaybackTracks.clear();
#endif

         // Don't cause a busy wait in the audio thread after stopping scrubbing
         mPlaybackSchedule.ResetMode();
      }
   });

   mPlaybackBuffers.reset();
   mPlaybackMixers.reset();
   mCaptureBuffers.reset();
   mResample.reset();

   double playbackTime = 4.0;

#ifdef EXPERIMENTAL_MIDI_OUT
   streamStartTime = 0;
   streamStartTime = SystemTime(mUsingAlsa);
#endif

   mPlaybackSchedule.Init(
      t0, t1, options, mCaptureTracks.empty() ? nullptr : &mRecordingSchedule );
   const bool scrubbing = mPlaybackSchedule.Interactive();
   if (scrubbing)
      playbackTime =
         lrint(options.pScrubbingOptions->delay * sampleRate) / sampleRate;

   //
   // The RingBuffer sizes, and the max amount of the buffer to
   // fill at a time, both grow linearly with the number of
   // tracks.  This allows us to scale up to many tracks without
   // killing performance.
   //

   // real playback time to produce with each filling of the buffers
   // by the Audio thread (except at the end of playback):
   // usually, make fillings fewer and longer for less CPU usage.
   // But for useful scrubbing, we can't run too far ahead without checking
   // mouse input, so make fillings more and shorter.
   // What Audio thread produces for playback is then consumed by the PortAudio
   // thread, in many smaller pieces.
   wxASSERT( playbackTime >= 0 );
   mPlaybackSamplesToCopy = playbackTime * mRate;

   // Capacity of the playback buffer.
   mPlaybackRingBufferSecs = 10.0;

   mCaptureRingBufferSecs = 4.5 + 0.5 * std::min(size_t(16), mCaptureTracks.size());
   mMinCaptureSecsToCopy = 0.2 + 0.2 * std::min(size_t(16), mCaptureTracks.size());

   unsigned int playbackChannels = 0;
   unsigned int captureChannels = 0;
   sampleFormat captureFormat = floatSample;

   if (tracks.playbackTracks.size() > 0 
#ifdef EXPERIMENTAL_MIDI_OUT
      || tracks.midiTracks.size() > 0
#endif
      )
      playbackChannels = 2;

   if (mSoftwarePlaythrough)
      playbackChannels = 2;

   if (tracks.captureTracks.size() > 0)
   {
      // For capture, every input channel gets its own track
      captureChannels = mCaptureTracks.size();
      // I don't deal with the possibility of the capture tracks
      // having different sample formats, since it will never happen
      // with the current code.  This code wouldn't *break* if this
      // assumption was false, but it would be sub-optimal.  For example,
      // if the first track was 16-bit and the second track was 24-bit,
      // we would set the sound card to capture in 16 bits and the second
      // track wouldn't get the benefit of all 24 bits the card is capable
      // of.
      captureFormat = mCaptureTracks[0]->GetSampleFormat();

      // Tell project that we are about to start recording
      if (mListener)
         mListener->OnAudioIOStartRecording();
   }

   bool successAudio;

   successAudio = StartPortAudioStream(sampleRate, playbackChannels,
                                       captureChannels, captureFormat);
#ifdef EXPERIMENTAL_MIDI_OUT

   // TODO: it may be that midi out will not work unless audio in or out is
   // active -- this would be a bug and may require a change in the
   // logic here.

   bool successMidi = true;

   if(!mMidiPlaybackTracks.empty()){
      successMidi = StartPortMidiStream();
   }

   // On the other hand, if MIDI cannot be opened, we will not complain
#endif

   if (!successAudio) {
      if (mListener && captureChannels > 0)
         mListener->OnAudioIOStopRecording();
      mStreamToken = 0;

      return 0;
   }

   //
   // The (audio) stream has been opened successfully (assuming we tried
   // to open it). We now proceed to
   // allocate the memory structures the stream will need.
   //

   bool bDone;
   do
   {
      bDone = true; // assume success
      try
      {
         if( mNumPlaybackChannels > 0 ) {
            // Allocate output buffers.  For every output track we allocate
            // a ring buffer of five seconds
            auto playbackBufferSize =
               (size_t)lrint(mRate * mPlaybackRingBufferSecs);
            auto playbackMixBufferSize =
               mPlaybackSamplesToCopy;

            mPlaybackBuffers.reinit(mPlaybackTracks.size());
            mPlaybackMixers.reinit(mPlaybackTracks.size());

            const Mixer::WarpOptions &warpOptions =
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
               scrubbing
                  ? Mixer::WarpOptions
                     (ScrubbingOptions::MinAllowedScrubSpeed(),
                      ScrubbingOptions::MaxAllowedScrubSpeed())
                  :
#endif
                    Mixer::WarpOptions(mPlaybackSchedule.mTimeTrack);

            for (unsigned int i = 0; i < mPlaybackTracks.size(); i++)
            {
               mPlaybackBuffers[i] = std::make_unique<RingBuffer>(floatSample, playbackBufferSize);

               // use track time for the end time, not real time!
               WaveTrackConstArray mixTracks;
               mixTracks.push_back(mPlaybackTracks[i]);

               double endTime;
               if (make_iterator_range(tracks.prerollTracks).contains(mPlaybackTracks[i]))
                  // Stop playing this track after pre-roll
                  endTime = t0;
               else
                  // Pass t1 -- not mT1 as may have been adjusted for latency
                  // -- so that overdub recording stops playing back samples
                  // at the right time, though transport may continue to record
                  endTime = t1;

               mPlaybackMixers[i] = std::make_unique<Mixer>
                  (mixTracks,
                  // Don't throw for read errors, just play silence:
                  false,
                  warpOptions,
                  mPlaybackSchedule.mT0,
                  endTime,
                  1,
                  playbackMixBufferSize, false,
                  mRate, floatSample, false);
               mPlaybackMixers[i]->ApplyTrackGains(false);
            }
         }

         if( mNumCaptureChannels > 0 )
         {
            // Allocate input buffers.  For every input track we allocate
            // a ring buffer of five seconds
            auto captureBufferSize = (size_t)(mRate * mCaptureRingBufferSecs + 0.5);

            // In the extraordinarily rare case that we can't even afford 100 samples, just give up.
            if(captureBufferSize < 100)
            {
               StartStreamCleanup();
               AudacityMessageBox(_("Out of memory!"));
               return 0;
            }

            mCaptureBuffers.reinit(mCaptureTracks.size());
            mResample.reinit(mCaptureTracks.size());
            mFactor = sampleRate / mRate;

            for( unsigned int i = 0; i < mCaptureTracks.size(); i++ )
            {
               mCaptureBuffers[i] = std::make_unique<RingBuffer>
                  ( mCaptureTracks[i]->GetSampleFormat(),
                                                    captureBufferSize );
               mResample[i] = std::make_unique<Resample>(true, mFactor, mFactor); // constant rate resampling
            }
         }
      }
      catch(std::bad_alloc&)
      {
         // Oops!  Ran out of memory.  This is pretty rare, so we'll just
         // try deleting everything, halving our buffer size, and try again.
         StartStreamCleanup(true);
         mPlaybackRingBufferSecs *= 0.5;
         mPlaybackSamplesToCopy /= 2;
         mCaptureRingBufferSecs *= 0.5;
         mMinCaptureSecsToCopy *= 0.5;
         bDone = false;

         // In the extraordinarily rare case that we can't even afford 100 samples, just give up.
         auto playbackBufferSize = (size_t)lrint(mRate * mPlaybackRingBufferSecs);
         auto playbackMixBufferSize = mPlaybackSamplesToCopy;
         if(playbackBufferSize < 100 || playbackMixBufferSize < 100)
         {
            StartStreamCleanup();
            AudacityMessageBox(_("Out of memory!"));
            return 0;
         }
      }
   } while(!bDone);

   if (mNumPlaybackChannels > 0)
   {
      EffectManager & em = EffectManager::Get();
      // Setup for realtime playback at the rate of the realtime
      // stream, not the rate of the track.
      em.RealtimeInitialize(mRate);

      // The following adds a NEW effect processor for each logical track and the
      // group determination should mimic what is done in audacityAudioCallback()
      // when calling RealtimeProcess().
      int group = 0;
      for (size_t i = 0, cnt = mPlaybackTracks.size(); i < cnt; i++)
      {
         const WaveTrack *vt = mPlaybackTracks[i].get();

         unsigned chanCnt = 1;
         if (vt->GetLinked())
         {
            i++;
            chanCnt++;
         }

         // Setup for realtime playback at the rate of the realtime
         // stream, not the rate of the track.
         em.RealtimeAddProcessor(group++, chanCnt, mRate);
      }
   }

#ifdef EXPERIMENTAL_AUTOMATED_INPUT_LEVEL_ADJUSTMENT
   AILASetStartTime();
#endif

   if (options.pStartTime)
   {
      // Calculate the NEW time position
      const auto time = mPlaybackSchedule.ClampTrackTime( *options.pStartTime );

      // Main thread's initialization of mTime
      mPlaybackSchedule.SetTrackTime( time );

      // Reset mixer positions for all playback tracks
      unsigned numMixers = mPlaybackTracks.size();
      for (unsigned ii = 0; ii < numMixers; ++ii)
         mPlaybackMixers[ii]->Reposition( time );
      mPlaybackSchedule.RealTimeInit( time );
   }

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   if (scrubbing)
   {
      const auto &scrubOptions = *options.pScrubbingOptions;
      mScrubQueue =
         std::make_unique<ScrubQueue>(
            mPlaybackSchedule.mT0, mPlaybackSchedule.mT1,
            scrubOptions.startClockTimeMillis,
            sampleRate, 2 * scrubOptions.minStutter,
            scrubOptions);
      mScrubDuration = 0;
      mSilentScrub = false;
   }
   else
      mScrubQueue.reset();
#endif

   // We signal the audio thread to call FillBuffers, to prime the RingBuffers
   // so that they will have data in them when the stream starts.  Having the
   // audio thread call FillBuffers here makes the code more predictable, since
   // FillBuffers will ALWAYS get called from the Audio thread.
   mAudioThreadShouldCallFillBuffersOnce = true;

   while( mAudioThreadShouldCallFillBuffersOnce ) {
      if (mScrubQueue)
         mScrubQueue->Nudge();
      wxMilliSleep( 50 );
   }

   if(mNumPlaybackChannels > 0 || mNumCaptureChannels > 0) {

#ifdef REALTIME_ALSA_THREAD
      // PRL: Do this in hope of less thread scheduling jitter in calls to
      // audacityAudioCallback.
      // Not needed to make audio playback work smoothly.
      // But needed in case we also play MIDI, so that the variable "offset"
      // in AudioIO::MidiTime() is a better approximation of the duration
      // between the call of audacityAudioCallback and the actual output of
      // the first audio sample.
      // (Which we should be able to determine from fields of
      // PaStreamCallbackTimeInfo, but that seems not to work as documented with
      // ALSA.)
      if (mUsingAlsa)
         // Perhaps we should do this only if also playing MIDI ?
         PaAlsa_EnableRealtimeScheduling( mPortStreamV19, 1 );
#endif

      //
      // Generate a unique value each time, to be returned to
      // clients accessing the AudioIO API, so they can query if they
      // are the ones who have reserved AudioIO or not.
      //
      // It is important to set this before setting the portaudio stream in
      // motion -- otherwise it may play an unspecified number of leading
      // zeroes.
      mStreamToken = (++mNextStreamToken);

      // This affects the AudioThread (not the portaudio callback).
      // Probably not needed so urgently before portaudio thread start for usual
      // playback, since our ring buffers have been primed already with 4 sec
      // of audio, but then we might be scrubbing, so do it.
      mAudioThreadFillBuffersLoopRunning = true;

      // Now start the PortAudio stream!
      PaError err;
      err = Pa_StartStream( mPortStreamV19 );

      if( err != paNoError )
      {
         mStreamToken = 0;
         mAudioThreadFillBuffersLoopRunning = false;
         if (mListener && mNumCaptureChannels > 0)
            mListener->OnAudioIOStopRecording();
         StartStreamCleanup();
         AudacityMessageBox(LAT1CTOWX(Pa_GetErrorText(err)));
         return 0;
      }
   }

   // Update UI display only now, after all possibilities for error are past.
   if (mListener) {
      // advertise the chosen I/O sample rate to the UI
      mListener->OnAudioIORate((int)mRate);
   }

   if (mNumPlaybackChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_PLAYBACK);
      e.SetEventObject(mOwningProject);
      e.SetInt(true);
      wxTheApp->ProcessEvent(e);
   }

   if (mNumCaptureChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_CAPTURE);
      e.SetEventObject(mOwningProject);
      e.SetInt(true);
      wxTheApp->ProcessEvent(e);
   }

   // Enable warning popups for unfound aliased blockfiles.
   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);

   commit = true;
   return mStreamToken;
}

void AudioIO::StartStreamCleanup(bool bOnlyBuffers)
{
   if (mNumPlaybackChannels > 0)
   {
      EffectManager::Get().RealtimeFinalize();
   }

   mPlaybackBuffers.reset();
   mPlaybackMixers.reset();
   mCaptureBuffers.reset();
   mResample.reset();

   if(!bOnlyBuffers)
   {
      Pa_AbortStream( mPortStreamV19 );
      Pa_CloseStream( mPortStreamV19 );
      mPortStreamV19 = NULL;
      mStreamToken = 0;
   }

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   mScrubQueue.reset();
#endif
}

#ifdef EXPERIMENTAL_MIDI_OUT

PmTimestamp MidiTime(void *WXUNUSED(info))
{
   return gAudioIO->MidiTime();
}

// Set up state to iterate NoteTrack events in sequence.
// Sends MIDI control changes up to the starting point mT0
// if send is true. Output is delayed by offset to facilitate
// looping (each iteration is delayed more).
void AudioIO::PrepareMidiIterator(bool send, double offset)
{
   int i;
   int nTracks = mMidiPlaybackTracks.size();
   // instead of initializing with an Alg_seq, we use begin_seq()
   // below to add ALL Alg_seq's.
   mIterator = std::make_unique<Alg_iterator>(nullptr, false);
   // Iterator not yet intialized, must add each track...
   for (i = 0; i < nTracks; i++) {
      const auto t = mMidiPlaybackTracks[i].get();
      Alg_seq_ptr seq = &t->GetSeq();
      // mark sequence tracks as "in use" since we're handing this
      // off to another thread and want to make sure nothing happens
      // to the data until playback finishes. This is just a sanity check.
      seq->set_in_use(true);
      mIterator->begin_seq(seq,
         // casting away const, but allegro just uses the pointer as an opaque "cookie"
         (void*)t, t->GetOffset() + offset);
   }
   GetNextEvent(); // prime the pump for FillMidiBuffers

   // Start MIDI from current cursor position
   mSendMidiState = true;
   while (mNextEvent &&
          mNextEventTime < mPlaybackSchedule.mT0 + offset) {
      if (send) OutputEvent();
      GetNextEvent();
   }
   mSendMidiState = false;
}

bool AudioIO::StartPortMidiStream()
{
   int i;
   int nTracks = mMidiPlaybackTracks.size();
   // Only start MIDI stream if there is an open track
   if (nTracks == 0)
      return false;

   //wxPrintf("StartPortMidiStream: mT0 %g mTime %g\n",
   //       mT0, mTime);

   /* get midi playback device */
   PmDeviceID playbackDevice = Pm_GetDefaultOutputDeviceID();
   wxString playbackDeviceName = gPrefs->Read(wxT("/MidiIO/PlaybackDevice"),
                                              wxT(""));
   mSynthLatency = gPrefs->Read(wxT("/MidiIO/SynthLatency"),
                                DEFAULT_SYNTH_LATENCY);
   if (wxStrcmp(playbackDeviceName, wxT("")) != 0) {
      for (i = 0; i < Pm_CountDevices(); i++) {
         const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
         if (!info) continue;
         if (!info->output) continue;
         wxString interf = wxSafeConvertMB2WX(info->interf);
         wxString name = wxSafeConvertMB2WX(info->name);
         interf.Append(wxT(": ")).Append(name);
         if (wxStrcmp(interf, playbackDeviceName) == 0) {
            playbackDevice = i;
         }
      }
   } // (else playback device has Pm_GetDefaultOuputDeviceID())

   /* open output device */
   mLastPmError = Pm_OpenOutput(&mMidiStream,
                                playbackDevice,
                                NULL,
                                0,
                                &::MidiTime,
                                NULL,
                                MIDI_MINIMAL_LATENCY_MS);
   if (mLastPmError == pmNoError) {
      mMidiStreamActive = true;
      mMidiPaused = false;
      mMidiLoopPasses = 0;
      mMidiOutputComplete = false;
      mMaxMidiTimestamp = 0;
      PrepareMidiIterator();

      // It is ok to call this now, but do not send timestamped midi
      // until after the first audio callback, which provides necessary
      // data for MidiTime().
      Pm_Synchronize(mMidiStream); // start using timestamps
      // start midi output flowing (pending first audio callback)
      mMidiThreadFillBuffersLoopRunning = true;
   }
   return (mLastPmError == pmNoError);
}
#endif

bool AudioIO::IsAvailable(AudacityProject *project) const
{
   return mOwningProject == NULL || mOwningProject == project;
}

void AudioIO::SetCaptureMeter(AudacityProject *project, MeterPanel *meter)
{
   if (!mOwningProject || mOwningProject == project)
   {
      if (meter)
      {
         mInputMeter = meter;
         mInputMeter->Reset(mRate, true);
      }
      else
         mInputMeter.Release();
   }
}

void AudioIO::SetPlaybackMeter(AudacityProject *project, MeterPanel *meter)
{
   if (!mOwningProject || mOwningProject == project)
   {
      mOutputMeter = meter;
      if (mOutputMeter)
      {
         mOutputMeter->Reset(mRate, true);
      }
   }
}

void AudioIO::SetMeters()
{
   if (mInputMeter)
      mInputMeter->Reset(mRate, true);
   if (mOutputMeter)
      mOutputMeter->Reset(mRate, true);

   MixerBoard* pMixerBoard = mOwningProject->GetMixerBoard();
   if (pMixerBoard)
      pMixerBoard->ResetMeters(true);

   mUpdateMeters = true;
}

void AudioIO::StopStream()
{
   auto cleanup = finally ( [this] {
      ClearRecordingException();
      mRecordingSchedule = {}; // free arrays
   } );

   if( mPortStreamV19 == NULL
#ifdef EXPERIMENTAL_MIDI_OUT
       && mMidiStream == NULL
#endif
     )
      return;

   if( Pa_IsStreamStopped( mPortStreamV19 )
#ifdef EXPERIMENTAL_MIDI_OUT
       && !mMidiStreamActive
#endif
     )
      return;

   wxMutexLocker locker(mSuspendAudioThread);

   // No longer need effects processing
   if (mNumPlaybackChannels > 0)
   {
      EffectManager::Get().RealtimeFinalize();
   }

   //
   // We got here in one of two ways:
   //
   // 1. The user clicked the stop button and we therefore want to stop
   //    as quickly as possible.  So we use AbortStream().  If this is
   //    the case the portaudio stream is still in the Running state
   //    (see PortAudio state machine docs).
   //
   // 2. The callback told PortAudio to stop the stream since it had
   //    reached the end of the selection.  The UI thread discovered
   //    this by noticing that AudioIO::IsActive() returned false.
   //    IsActive() (which calls Pa_GetStreamActive()) will not return
   //    false until all buffers have finished playing, so we can call
   //    AbortStream without losing any samples.  If this is the case
   //    we are in the "callback finished state" (see PortAudio state
   //    machine docs).
   //
   // The moral of the story: We can call AbortStream safely, without
   // losing samples.
   //
   // DMM: This doesn't seem to be true; it seems to be necessary to
   // call StopStream if the callback brought us here, and AbortStream
   // if the user brought us here.
   //

   mAudioThreadFillBuffersLoopRunning = false;
   if (mScrubQueue)
      mScrubQueue->Nudge();

   // Audacity can deadlock if it tries to update meters while
   // we're stopping PortAudio (because the meter updating code
   // tries to grab a UI mutex while PortAudio tries to join a
   // pthread).  So we tell the callback to stop updating meters,
   // and wait until the callback has left this part of the code
   // if it was already there.
   mUpdateMeters = false;
   while(mUpdatingMeters) {
      ::wxSafeYield();
      wxMilliSleep( 50 );
   }

   // Turn off HW playthrough if PortMixer is being used

  #if defined(USE_PORTMIXER)
   if( mPortMixer ) {
      #if __WXMAC__
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
   }
  #endif

   if (mPortStreamV19) {
      Pa_AbortStream( mPortStreamV19 );
      Pa_CloseStream( mPortStreamV19 );
      mPortStreamV19 = NULL;
   }

   if (mNumPlaybackChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_PLAYBACK);
      e.SetEventObject(mOwningProject);
      e.SetInt(false);
      wxTheApp->ProcessEvent(e);
   }

   if (mNumCaptureChannels > 0)
   {
      wxCommandEvent e(mStreamToken == 0 ? EVT_AUDIOIO_MONITOR : EVT_AUDIOIO_CAPTURE);
      e.SetEventObject(mOwningProject);
      e.SetInt(false);
      wxTheApp->ProcessEvent(e);
   }

#ifdef EXPERIMENTAL_MIDI_OUT
   /* Stop Midi playback */
   if ( mMidiStream ) {

      mMidiStreamActive = false;

#ifdef USE_MIDI_THREAD
      mMidiThreadFillBuffersLoopRunning = false; // stop output to stream
      // but output is in another thread. Wait for output to stop...
      while (mMidiThreadFillBuffersLoopActive) {
         wxMilliSleep(1);
      }
#endif

      mMidiOutputComplete = true;

      // now we can assume "ownership" of the mMidiStream
      // if output in progress, send all off, etc.
      AllNotesOff();
      // AllNotesOff() should be sufficient to stop everything, but
      // in Linux, if you Pm_Close() immediately, it looks like
      // messages are dropped. ALSA then seems to send All Sound Off
      // and Reset All Controllers messages, but not all synthesizers
      // respond to these messages. This is probably a bug in PortMidi
      // if the All Off messages do not get out, but for security,
      // delay a bit so that messages can be delivered before closing
      // the stream. Add 2ms of "padding" to avoid any rounding errors.
      while (mMaxMidiTimestamp + 2 > MidiTime()) {
          wxMilliSleep(1); // deliver the all-off messages
      }
      Pm_Close(mMidiStream);
      mMidiStream = NULL;
      mIterator->end();

      // set in_use flags to false
      int nTracks = mMidiPlaybackTracks.size();
      for (int i = 0; i < nTracks; i++) {
         const auto t = mMidiPlaybackTracks[i].get();
         Alg_seq_ptr seq = &t->GetSeq();
         seq->set_in_use(false);
      }

      mIterator.reset(); // just in case someone tries to reference it
   }
#endif

   // If there's no token, we were just monitoring, so we can
   // skip this next part...
   if (mStreamToken > 0) {
      // In either of the above cases, we want to make sure that any
      // capture data that made it into the PortAudio callback makes it
      // to the target WaveTrack.  To do this, we ask the audio thread to
      // call FillBuffers one last time (it normally would not do so since
      // Pa_GetStreamActive() would now return false
      mAudioThreadShouldCallFillBuffersOnce = true;

      while( mAudioThreadShouldCallFillBuffersOnce )
      {
         // LLL:  Experienced recursive yield here...once.
         wxGetApp().Yield(true); // Pass true for onlyIfNeeded to avoid recursive call error.
         if (mScrubQueue)
            mScrubQueue->Nudge();
         wxMilliSleep( 50 );
      }

      //
      // Everything is taken care of.  Now, just free all the resources
      // we allocated in StartStream()
      //

      if (mPlaybackTracks.size() > 0)
      {
         mPlaybackBuffers.reset();
         mPlaybackMixers.reset();
      }

      //
      // Offset all recorded tracks to account for latency
      //
      if (mCaptureTracks.size() > 0)
      {
         mCaptureBuffers.reset();
         mResample.reset();

         //
         // We only apply latency correction when we actually played back
         // tracks during the recording. If we did not play back tracks,
         // there's nothing we could be out of sync with. This also covers the
         // case that we do not apply latency correction when recording the
         // first track in a project.
         //

         for (unsigned int i = 0; i < mCaptureTracks.size(); i++) {
            // The calls to Flush
            // may cause exceptions because of exhaustion of disk space.
            // Stop those exceptions here, or else they propagate through too
            // many parts of Audacity that are not effects or editing
            // operations.  GuardedCall ensures that the user sees a warning.

            // Also be sure to Flush each track, at the top of the guarded call,
            // relying on the guarantee that the track will be left in a flushed
            // state, though the append buffer may be lost.

            GuardedCall( [&] {
               WaveTrack* track = mCaptureTracks[i].get();

               // use NOFAIL-GUARANTEE that track is flushed,
               // PARTIAL-GUARANTEE that some initial length of the recording
               // is saved.
               // See comments in FillBuffers().
               track->Flush();
            } );
         }

         for (auto &interval : mLostCaptureIntervals) {
            auto &start = interval.first;
            auto duration = interval.second;
            for (auto &track : mCaptureTracks) {
               GuardedCall([&] {
                  track->SyncLockAdjust(start, start + duration);
               });
            }
         }

         ControlToolBar *bar = mOwningProject->GetControlToolBar();
         bar->CommitRecording();
      }
   }

   if (mInputMeter)
      mInputMeter->Reset(mRate, false);

   if (mOutputMeter)
      mOutputMeter->Reset(mRate, false);

   MixerBoard* pMixerBoard = mOwningProject->GetMixerBoard();
   if (pMixerBoard)
      pMixerBoard->ResetMeters(false);

   mInputMeter.Release();
   mOutputMeter = NULL;
   mOwningProject = NULL;

   if (mListener && mNumCaptureChannels > 0)
      mListener->OnAudioIOStopRecording();

   //
   // Only set token to 0 after we're totally finished with everything
   //
   mStreamToken = 0;

   mNumCaptureChannels = 0;
   mNumPlaybackChannels = 0;

   mPlaybackTracks.clear();
   mCaptureTracks.clear();
#ifdef HAVE_MIDI
   mMidiPlaybackTracks.clear();
#endif

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   mScrubQueue.reset();
#endif

   if (mListener) {
      // Tell UI to hide sample rate
      mListener->OnAudioIORate(0);
   }

   // Don't cause a busy wait in the audio thread after stopping scrubbing
   mPlaybackSchedule.ResetMode();
}

void AudioIO::SetPaused(bool state)
{
   if (state != mPaused)
   {
      if (state)
      {
         EffectManager::Get().RealtimeSuspend();
      }
      else
      {
         EffectManager::Get().RealtimeResume();
      }
   }

   mPaused = state;
}

bool AudioIO::IsPaused() const
{
   return mPaused;
}

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
bool AudioIO::EnqueueScrub
   (double endTimeOrSpeed, const ScrubbingOptions &options)
{
   if (mScrubQueue)
      return mScrubQueue->Producer(endTimeOrSpeed, options);
   else
      return false;
}

double AudioIO::GetLastTimeInScrubQueue() const
{
   if (mScrubQueue)
      return mScrubQueue->LastTimeInQueue();
   else
      return -1.0;
}

#endif

bool AudioIO::IsBusy() const
{
   if (mStreamToken != 0)
      return true;

   return false;
}

bool AudioIO::IsStreamActive() const
{
   bool isActive = false;
   // JKC: Not reporting any Pa error, but that looks OK.
   if( mPortStreamV19 )
      isActive = (Pa_IsStreamActive( mPortStreamV19 ) > 0);

#ifdef EXPERIMENTAL_MIDI_OUT
   if( mMidiStreamActive && !mMidiOutputComplete )
      isActive = true;
#endif
   return isActive;
}

bool AudioIO::IsStreamActive(int token) const
{
   return (this->IsStreamActive() && this->IsAudioTokenActive(token));
}

bool AudioIO::IsAudioTokenActive(int token) const
{
   return ( token > 0 && token == mStreamToken );
}

bool AudioIO::IsMonitoring() const
{
   return ( mPortStreamV19 && mStreamToken==0 );
}

void AudioIO::PlaybackSchedule::Init(
   const double t0, const double t1,
   const AudioIOStartStreamOptions &options,
   const RecordingSchedule *pRecordingSchedule )
{
   if ( pRecordingSchedule )
      // It does not make sense to apply the time warp during overdub recording,
      // which defeats the purpose of making the recording synchronized with
      // the existing audio.  (Unless we figured out the inverse warp of the
      // captured samples in real time.)
      // So just quietly ignore the time track.
      mTimeTrack = nullptr;
   else
      mTimeTrack = options.timeTrack;

   mT0      = t0;
   if (pRecordingSchedule)
      mT0 -= pRecordingSchedule->mPreRoll;

   mT1      = t1;
   if (pRecordingSchedule)
      // adjust mT1 so that we don't give paComplete too soon to fill up the
      // desired length of recording
      mT1 -= pRecordingSchedule->mLatencyCorrection;

   // Main thread's initialization of mTime
   SetTrackTime( mT0 );

   mPlayMode = options.playLooped
      ? PlaybackSchedule::PLAY_LOOPED
      : PlaybackSchedule::PLAY_STRAIGHT;
   mCutPreviewGapStart = options.cutPreviewGapStart;
   mCutPreviewGapLen = options.cutPreviewGapLen;

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   bool scrubbing = (options.pScrubbingOptions != nullptr);

   // Scrubbing is not compatible with looping or recording or a time track!
   if (scrubbing)
   {
      const auto &scrubOptions = *options.pScrubbingOptions;
      if (pRecordingSchedule ||
          Looping() ||
          mTimeTrack != NULL ||
          scrubOptions.maxSpeed < ScrubbingOptions::MinAllowedScrubSpeed()) {
         wxASSERT(false);
         scrubbing = false;
      }
      else
         mPlayMode = (scrubOptions.isPlayingAtSpeed)
            ? PlaybackSchedule::PLAY_AT_SPEED
            : PlaybackSchedule::PLAY_SCRUB;
   }
#endif

   mWarpedTime = 0.0;
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   if (Scrubbing())
      mWarpedLength = 0.0f;
   else
#endif
      mWarpedLength = RealDuration(mT1);
}

double AudioIO::PlaybackSchedule::LimitTrackTime() const
{
   // Track time readout for the main thread
   // Allows for forward or backward play
   return ClampTrackTime( GetTrackTime() );
}

double AudioIO::PlaybackSchedule::ClampTrackTime( double trackTime ) const
{
   if (ReversedTime())
      return std::max(mT1, std::min(mT0, trackTime));
   else
      return std::max(mT0, std::min(mT1, trackTime));
}

double AudioIO::PlaybackSchedule::NormalizeTrackTime() const
{
   // Track time readout for the main thread

   // dmazzoni: This function is needed for two reasons:
   // One is for looped-play mode - this function makes sure that the
   // position indicator keeps wrapping around.  The other reason is
   // more subtle - it's because PortAudio can query the hardware for
   // the current stream time, and this query is not always accurate.
   // Sometimes it's a little behind or ahead, and so this function
   // makes sure that at least we clip it to the selection.
   //
   // msmeyer: There is also the possibility that we are using "cut preview"
   //          mode. In this case, we should jump over a defined "gap" in the
   //          audio.

   double absoluteTime;

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   // Limit the time between t0 and t1 if not scrubbing.
   // Should the limiting be necessary in any play mode if there are no bugs?
   if (Interactive())
      absoluteTime = GetTrackTime();
   else
#endif
      absoluteTime = LimitTrackTime();

   if (mCutPreviewGapLen > 0)
   {
      // msmeyer: We're in cut preview mode, so if we are on the right
      // side of the gap, we jump over it.
      if (absoluteTime > mCutPreviewGapStart)
         absoluteTime += mCutPreviewGapLen;
   }

   return absoluteTime;
}

double AudioIO::GetStreamTime()
{
   // Track time readout for the main thread

   if( !IsStreamActive() )
      return BAD_STREAM_TIME;

   return mPlaybackSchedule.NormalizeTrackTime();
}


std::vector<long> AudioIO::GetSupportedPlaybackRates(int devIndex, double rate)
{
   if (devIndex == -1)
   {  // weren't given a device index, get the prefs / default one
      devIndex = getPlayDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedPlaybackIndex != -1 && devIndex == mCachedPlaybackIndex
         && (rate == 0.0 || make_iterator_range(mCachedPlaybackRates).contains(rate)))
   {
      return mCachedPlaybackRates;
   }

   std::vector<long> supported;
   int irate = (int)rate;
   const PaDeviceInfo* devInfo = NULL;
   int i;

   devInfo = Pa_GetDeviceInfo(devIndex);

   if (!devInfo)
   {
      wxLogDebug(wxT("GetSupportedPlaybackRates() Could not get device info!"));
      return supported;
   }

   // LLL: Remove when a proper method of determining actual supported
   //      DirectSound rate is devised.
   const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(devInfo->hostApi);
   bool isDirectSound = (hostInfo && hostInfo->type == paDirectSound);

   PaStreamParameters pars;

   pars.device = devIndex;
   pars.channelCount = 1;
   pars.sampleFormat = paFloat32;
   pars.suggestedLatency = devInfo->defaultHighOutputLatency;
   pars.hostApiSpecificStreamInfo = NULL;

   // JKC: PortAudio Errors handled OK here.  No need to report them
   for (i = 0; i < NumRatesToTry; i++)
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(NULL, &pars, RatesToTry[i]) == 0)
         supported.push_back(RatesToTry[i]);
   }

   if (irate != 0 && !make_iterator_range(supported).contains(irate))
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(NULL, &pars, irate) == 0)
         supported.push_back(irate);
   }

   return supported;
}

std::vector<long> AudioIO::GetSupportedCaptureRates(int devIndex, double rate)
{
   if (devIndex == -1)
   {  // not given a device, look up in prefs / default
      devIndex = getRecordDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedCaptureIndex != -1 && devIndex == mCachedCaptureIndex
         && (rate == 0.0 || make_iterator_range(mCachedCaptureRates).contains(rate)))
   {
      return mCachedCaptureRates;
   }

   std::vector<long> supported;
   int irate = (int)rate;
   const PaDeviceInfo* devInfo = NULL;
   int i;

   devInfo = Pa_GetDeviceInfo(devIndex);

   if (!devInfo)
   {
      wxLogDebug(wxT("GetSupportedCaptureRates() Could not get device info!"));
      return supported;
   }

   double latencyDuration = DEFAULT_LATENCY_DURATION;
   long recordChannels = 1;
   gPrefs->Read(wxT("/AudioIO/LatencyDuration"), &latencyDuration);
   gPrefs->Read(wxT("/AudioIO/RecordChannels"), &recordChannels);

   // LLL: Remove when a proper method of determining actual supported
   //      DirectSound rate is devised.
   const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(devInfo->hostApi);
   bool isDirectSound = (hostInfo && hostInfo->type == paDirectSound);

   PaStreamParameters pars;

   pars.device = devIndex;
   pars.channelCount = recordChannels;
   pars.sampleFormat = paFloat32;
   pars.suggestedLatency = latencyDuration / 1000.0;
   pars.hostApiSpecificStreamInfo = NULL;

   for (i = 0; i < NumRatesToTry; i++)
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(&pars, NULL, RatesToTry[i]) == 0)
         supported.push_back(RatesToTry[i]);
   }

   if (irate != 0 && !make_iterator_range(supported).contains(irate))
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(&pars, NULL, irate) == 0)
         supported.push_back(irate);
   }

   return supported;
}

std::vector<long> AudioIO::GetSupportedSampleRates(int playDevice, int recDevice, double rate)
{
   // Not given device indices, look up prefs
   if (playDevice == -1) {
      playDevice = getPlayDevIndex();
   }
   if (recDevice == -1) {
      recDevice = getRecordDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedPlaybackIndex != -1 && mCachedCaptureIndex != -1 &&
         playDevice == mCachedPlaybackIndex &&
         recDevice == mCachedCaptureIndex &&
         (rate == 0.0 || make_iterator_range(mCachedSampleRates).contains(rate)))
   {
      return mCachedSampleRates;
   }

   auto playback = GetSupportedPlaybackRates(playDevice, rate);
   auto capture = GetSupportedCaptureRates(recDevice, rate);
   int i;

   // Return only sample rates which are in both arrays
   std::vector<long> result;

   for (i = 0; i < (int)playback.size(); i++)
      if (make_iterator_range(capture).contains(playback[i]))
         result.push_back(playback[i]);

   // If this yields no results, use the default sample rates nevertheless
/*   if (result.IsEmpty())
   {
      for (i = 0; i < NumStandardRates; i++)
         result.Add(StandardRates[i]);
   }*/

   return result;
}

/** \todo: should this take into account PortAudio's value for
 * PaDeviceInfo::defaultSampleRate? In principal this should let us work out
 * which rates are "real" and which resampled in the drivers, and so prefer
 * the real rates. */
int AudioIO::GetOptimalSupportedSampleRate()
{
   auto rates = GetSupportedSampleRates();

   if (make_iterator_range(rates).contains(44100))
      return 44100;

   if (make_iterator_range(rates).contains(48000))
      return 48000;

   // if there are no supported rates, the next bit crashes. So check first,
   // and give them a "sensible" value if there are no valid values. They
   // will still get an error later, but with any luck may have changed
   // something by then. It's no worse than having an invalid default rate
   // stored in the preferences, which we don't check for
   if (rates.empty()) return 44100;

   return rates.back();
}

double AudioIO::GetBestRate(bool capturing, bool playing, double sampleRate)
{
   // Check if we can use the cached value
   if (mCachedBestRateIn != 0.0 && mCachedBestRateIn == sampleRate) {
      return mCachedBestRateOut;
   }

   // In order to cache the value, all early returns should instead set retval
   // and jump to finished
   double retval;

   std::vector<long> rates;
   if (capturing) wxLogDebug(wxT("AudioIO::GetBestRate() for capture"));
   if (playing) wxLogDebug(wxT("AudioIO::GetBestRate() for playback"));
   wxLogDebug(wxT("GetBestRate() suggested rate %.0lf Hz"), sampleRate);

   if (capturing && !playing) {
      rates = GetSupportedCaptureRates(-1, sampleRate);
   }
   else if (playing && !capturing) {
      rates = GetSupportedPlaybackRates(-1, sampleRate);
   }
   else {   // we assume capturing and playing - the alternative would be a
            // bit odd
      rates = GetSupportedSampleRates(-1, -1, sampleRate);
   }
   /* rem rates is the array of hardware-supported sample rates (in the current
    * configuration), sampleRate is the Project Rate (desired sample rate) */
   long rate = (long)sampleRate;

   if (make_iterator_range(rates).contains(rate)) {
      wxLogDebug(wxT("GetBestRate() Returning %.0ld Hz"), rate);
      retval = rate;
      goto finished;
      /* the easy case - the suggested rate (project rate) is in the list, and
       * we can just accept that and send back to the caller. This should be
       * the case for most users most of the time (all of the time on
       * Win MME as the OS does resampling) */
   }

   /* if we get here, there is a problem - the project rate isn't supported
    * on our hardware, so we can't us it. Need to come up with an alternative
    * rate to use. The process goes like this:
    * * If there are no rates to pick from, we're stuck and return 0 (error)
    * * If there are some rates, we pick the next one higher than the requested
    *   rate to use.
    * * If there aren't any higher, we use the highest available rate */

   if (rates.empty()) {
      /* we're stuck - there are no supported rates with this hardware. Error */
      wxLogDebug(wxT("GetBestRate() Error - no supported sample rates"));
      retval = 0.0;
      goto finished;
   }
   int i;
   for (i = 0; i < (int)rates.size(); i++)  // for each supported rate
         {
         if (rates[i] > rate) {
            // supported rate is greater than requested rate
            wxLogDebug(wxT("GetBestRate() Returning next higher rate - %.0ld Hz"), rates[i]);
            retval = rates[i];
            goto finished;
         }
         }

   wxLogDebug(wxT("GetBestRate() Returning highest rate - %.0ld Hz"), rates.back());
   retval = rates.back(); // the highest available rate
   goto finished;

finished:
   mCachedBestRateIn = sampleRate;
   mCachedBestRateOut = retval;
   return retval;
}


//////////////////////////////////////////////////////////////////////
//
//     Audio Thread Context
//
//////////////////////////////////////////////////////////////////////

AudioThread::ExitCode AudioThread::Entry()
{
   while( !TestDestroy() )
   {
      // Set LoopActive outside the tests to avoid race condition
      gAudioIO->mAudioThreadFillBuffersLoopActive = true;
      if( gAudioIO->mAudioThreadShouldCallFillBuffersOnce )
      {
         gAudioIO->FillBuffers();
         gAudioIO->mAudioThreadShouldCallFillBuffersOnce = false;
      }
      else if( gAudioIO->mAudioThreadFillBuffersLoopRunning )
      {
         gAudioIO->FillBuffers();
      }
      gAudioIO->mAudioThreadFillBuffersLoopActive = false;

      if (gAudioIO->mPlaybackSchedule.Interactive()) {
         // Rely on the Wait() in ScrubQueue::Transformer()
         // This allows the scrubbing update interval to be made very short without
         // playback becoming intermittent.
      }
      else {
         // Perhaps this too could use a condition variable, for available space in the
         // ring buffer, instead of a polling loop?  But no harm in doing it this way.
         Sleep(10);
      }
   }

   return 0;
}


#ifdef EXPERIMENTAL_MIDI_OUT
MidiThread::ExitCode MidiThread::Entry()
{
   while( !TestDestroy() )
   {
      // Set LoopActive outside the tests to avoid race condition
      gAudioIO->mMidiThreadFillBuffersLoopActive = true;
      if( gAudioIO->mMidiThreadFillBuffersLoopRunning &&
          // mNumFrames signals at least one callback, needed for MidiTime()
          gAudioIO->mNumFrames > 0)
      {
         gAudioIO->FillMidiBuffers();
      }
      gAudioIO->mMidiThreadFillBuffersLoopActive = false;
      Sleep(MIDI_SLEEP);
   }
   return 0;
}
#endif

size_t AudioIO::GetCommonlyAvailPlayback()
{
   auto commonlyAvail = mPlaybackBuffers[0]->AvailForPut();
   for (unsigned i = 1; i < mPlaybackTracks.size(); ++i)
      commonlyAvail = std::min(commonlyAvail,
         mPlaybackBuffers[i]->AvailForPut());
   return commonlyAvail;
}

size_t AudioIO::GetCommonlyAvailCapture()
{
   auto commonlyAvail = mCaptureBuffers[0]->AvailForGet();
   for (unsigned i = 1; i < mCaptureTracks.size(); ++i)
      commonlyAvail = std::min(commonlyAvail,
         mCaptureBuffers[i]->AvailForGet());
   return commonlyAvail;
}

#if USE_PORTMIXER
int AudioIO::getRecordSourceIndex(PxMixer *portMixer)
{
   int i;
   wxString sourceName = gPrefs->Read(wxT("/AudioIO/RecordingSource"), wxT(""));
   int numSources = Px_GetNumInputSources(portMixer);
   for (i = 0; i < numSources; i++) {
      if (sourceName == wxString(wxSafeConvertMB2WX(Px_GetInputSourceName(portMixer, i))))
         return i;
   }
   return -1;
}
#endif

int AudioIO::getPlayDevIndex(const wxString &devNameArg)
{
   wxString devName(devNameArg);
   // if we don't get given a device, look up the preferences
   if (devName.IsEmpty())
   {
      devName = gPrefs->Read(wxT("/AudioIO/PlaybackDevice"), wxT(""));
   }

   wxString hostName = gPrefs->Read(wxT("/AudioIO/Host"), wxT(""));
   PaHostApiIndex hostCnt = Pa_GetHostApiCount();
   PaHostApiIndex hostNum;
   for (hostNum = 0; hostNum < hostCnt; hostNum++)
   {
      const PaHostApiInfo *hinfo = Pa_GetHostApiInfo(hostNum);
      if (hinfo && wxString(wxSafeConvertMB2WX(hinfo->name)) == hostName)
      {
         for (PaDeviceIndex hostDevice = 0; hostDevice < hinfo->deviceCount; hostDevice++)
         {
            PaDeviceIndex deviceNum = Pa_HostApiDeviceIndexToDeviceIndex(hostNum, hostDevice);

            const PaDeviceInfo *dinfo = Pa_GetDeviceInfo(deviceNum);
            if (dinfo && DeviceName(dinfo) == devName && dinfo->maxOutputChannels > 0 )
            {
               // this device name matches the stored one, and works.
               // So we say this is the answer and return it
               return deviceNum;
            }
         }

         // The device wasn't found so use the default for this host.
         // LL:  At this point, preferences and active no longer match.
         return hinfo->defaultOutputDevice;
      }
   }

   // The host wasn't found, so use the default output device.
   // FIXME: TRAP_ERR PaErrorCode not handled well (this code is similar to input code
   // and the input side has more comments.)

   PaDeviceIndex deviceNum = Pa_GetDefaultOutputDevice();

   // Sometimes PortAudio returns -1 if it cannot find a suitable default
   // device, so we just use the first one available
   //
   // LL:  At this point, preferences and active no longer match
   //
   //      And I can't imagine how far we'll get specifying an "invalid" index later
   //      on...are we certain "0" even exists?
   if (deviceNum < 0) {
      wxASSERT(false);
      deviceNum = 0;
   }

   return deviceNum;
}

int AudioIO::getRecordDevIndex(const wxString &devNameArg)
{
   wxString devName(devNameArg);
   // if we don't get given a device, look up the preferences
   if (devName.IsEmpty())
   {
      devName = gPrefs->Read(wxT("/AudioIO/RecordingDevice"), wxT(""));
   }

   wxString hostName = gPrefs->Read(wxT("/AudioIO/Host"), wxT(""));
   PaHostApiIndex hostCnt = Pa_GetHostApiCount();
   PaHostApiIndex hostNum;
   for (hostNum = 0; hostNum < hostCnt; hostNum++)
   {
      const PaHostApiInfo *hinfo = Pa_GetHostApiInfo(hostNum);
      if (hinfo && wxString(wxSafeConvertMB2WX(hinfo->name)) == hostName)
      {
         for (PaDeviceIndex hostDevice = 0; hostDevice < hinfo->deviceCount; hostDevice++)
         {
            PaDeviceIndex deviceNum = Pa_HostApiDeviceIndexToDeviceIndex(hostNum, hostDevice);

            const PaDeviceInfo *dinfo = Pa_GetDeviceInfo(deviceNum);
            if (dinfo && DeviceName(dinfo) == devName && dinfo->maxInputChannels > 0 )
            {
               // this device name matches the stored one, and works.
               // So we say this is the answer and return it
               return deviceNum;
            }
         }

         // The device wasn't found so use the default for this host.
         // LL:  At this point, preferences and active no longer match.
         return hinfo->defaultInputDevice;
      }
   }

   // The host wasn't found, so use the default input device.
   // FIXME: TRAP_ERR PaErrorCode not handled well in getRecordDevIndex()
   PaDeviceIndex deviceNum = Pa_GetDefaultInputDevice();

   // Sometimes PortAudio returns -1 if it cannot find a suitable default
   // device, so we just use the first one available
   // PortAudio has an error reporting function.  We should log/report the error?
   //
   // LL:  At this point, preferences and active no longer match
   //
   //      And I can't imagine how far we'll get specifying an "invalid" index later
   //      on...are we certain "0" even exists?
   if (deviceNum < 0) {
      // JKC: This ASSERT will happen if you run with no config file
      // This happens once.  Config file will exist on the next run.
      // TODO: Look into this a bit more.  Could be relevant to blank Device Toolbar.
      wxASSERT(false);
      deviceNum = 0;
   }

   return deviceNum;
}

wxString AudioIO::GetDeviceInfo()
{
   wxStringOutputStream o;
   wxTextOutputStream s(o, wxEOL_UNIX);
   wxString e(wxT("\n"));

   if (IsStreamActive()) {
      return wxT("Stream is active ... unable to gather information.");
   }


   // FIXME: TRAP_ERR PaErrorCode not handled.  3 instances in GetDeviceInfo().
   int recDeviceNum = Pa_GetDefaultInputDevice();
   int playDeviceNum = Pa_GetDefaultOutputDevice();
   int cnt = Pa_GetDeviceCount();

   wxLogDebug(wxT("Portaudio reports %d audio devices"),cnt);

   s << wxT("==============================") << e;
   s << wxT("Default recording device number: ") << recDeviceNum << e;
   s << wxT("Default playback device number: ") << playDeviceNum << e;

   wxString recDevice = gPrefs->Read(wxT("/AudioIO/RecordingDevice"), wxT(""));
   wxString playDevice = gPrefs->Read(wxT("/AudioIO/PlaybackDevice"), wxT(""));
   int j;

   // This gets info on all available audio devices (input and output)
   if (cnt <= 0) {
      s << wxT("No devices found\n");
      return o.GetString();
   }

   const PaDeviceInfo* info;

   for (j = 0; j < cnt; j++) {
      s << wxT("==============================") << e;

      info = Pa_GetDeviceInfo(j);
      if (!info) {
         s << wxT("Device info unavailable for: ") << j << wxT("\n");
         continue;
      }

      wxString name = DeviceName(info);
      s << wxT("Device ID: ") << j << e;
      s << wxT("Device name: ") << name << e;
      s << wxT("Host name: ") << HostName(info) << e;
      s << wxT("Recording channels: ") << info->maxInputChannels << e;
      s << wxT("Playback channels: ") << info->maxOutputChannels << e;
      s << wxT("Low Recording Latency: ") << info->defaultLowInputLatency << e;
      s << wxT("Low Playback Latency: ") << info->defaultLowOutputLatency << e;
      s << wxT("High Recording Latency: ") << info->defaultHighInputLatency << e;
      s << wxT("High Playback Latency: ") << info->defaultHighOutputLatency << e;

      auto rates = GetSupportedPlaybackRates(j, 0.0);

      s << wxT("Supported Rates:") << e;
      for (int k = 0; k < (int) rates.size(); k++) {
         s << wxT("    ") << (int)rates[k] << e;
      }

      if (name == playDevice && info->maxOutputChannels > 0)
         playDeviceNum = j;

      if (name == recDevice && info->maxInputChannels > 0)
         recDeviceNum = j;

      // Sometimes PortAudio returns -1 if it cannot find a suitable default
      // device, so we just use the first one available
      if (recDeviceNum < 0 && info->maxInputChannels > 0){
         recDeviceNum = j;
      }
      if (playDeviceNum < 0 && info->maxOutputChannels > 0){
         playDeviceNum = j;
      }
   }

   bool haveRecDevice = (recDeviceNum >= 0);
   bool havePlayDevice = (playDeviceNum >= 0);

   s << wxT("==============================") << e;
   if(haveRecDevice){
      s << wxT("Selected recording device: ") << recDeviceNum << wxT(" - ") << recDevice << e;
   }else{
      s << wxT("No recording device found for '") << recDevice << wxT("'.") << e;
   }
   if(havePlayDevice){
      s << wxT("Selected playback device: ") << playDeviceNum << wxT(" - ") << playDevice << e;
   }else{
      s << wxT("No playback device found for '") << playDevice << wxT("'.") << e;
   }

   std::vector<long> supportedSampleRates;

   if(havePlayDevice && haveRecDevice){
      supportedSampleRates = GetSupportedSampleRates(playDeviceNum, recDeviceNum);

      s << wxT("Supported Rates:") << e;
      for (int k = 0; k < (int) supportedSampleRates.size(); k++) {
         s << wxT("    ") << (int)supportedSampleRates[k] << e;
      }
   }else{
      s << wxT("Cannot check mutual sample rates without both devices.") << e;
      return o.GetString();
   }

#if defined(USE_PORTMIXER)
   if (supportedSampleRates.size() > 0)
      {
      int highestSampleRate = supportedSampleRates.back();
      bool EmulateMixerInputVol = true;
      bool EmulateMixerOutputVol = true;
      float MixerInputVol = 1.0;
      float MixerOutputVol = 1.0;

      int error;

      PaStream *stream;

      PaStreamParameters playbackParameters;

      playbackParameters.device = playDeviceNum;
      playbackParameters.sampleFormat = paFloat32;
      playbackParameters.hostApiSpecificStreamInfo = NULL;
      playbackParameters.channelCount = 1;
      if (Pa_GetDeviceInfo(playDeviceNum)){
         playbackParameters.suggestedLatency =
            Pa_GetDeviceInfo(playDeviceNum)->defaultLowOutputLatency;
      }
      else{
         playbackParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;
      }

      PaStreamParameters captureParameters;

      captureParameters.device = recDeviceNum;
      captureParameters.sampleFormat = paFloat32;;
      captureParameters.hostApiSpecificStreamInfo = NULL;
      captureParameters.channelCount = 1;
      if (Pa_GetDeviceInfo(recDeviceNum)){
         captureParameters.suggestedLatency =
            Pa_GetDeviceInfo(recDeviceNum)->defaultLowInputLatency;
      }else{
         captureParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;
      }

      error = Pa_OpenStream(&stream,
                         &captureParameters, &playbackParameters,
                         highestSampleRate, paFramesPerBufferUnspecified,
                         paClipOff | paDitherOff,
                         audacityAudioCallback, NULL);

      if (error) {
         error = Pa_OpenStream(&stream,
                            &captureParameters, NULL,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);
      }

      if (error) {
         s << wxT("Received ") << error << wxT(" while opening devices") << e;
         return o.GetString();
      }

      PxMixer *PortMixer = Px_OpenMixer(stream, 0);

      if (!PortMixer) {
         s << wxT("Unable to open Portmixer") << e;
         Pa_CloseStream(stream);
         return o.GetString();
      }

      s << wxT("==============================") << e;
      s << wxT("Available mixers:") << e;

      // FIXME: ? PortMixer errors on query not reported in GetDeviceInfo
      cnt = Px_GetNumMixers(stream);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetMixerName(stream, i));
         s << i << wxT(" - ") << name << e;
      }

      s << wxT("==============================") << e;
      s << wxT("Available recording sources:") << e;
      cnt = Px_GetNumInputSources(PortMixer);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetInputSourceName(PortMixer, i));
         s << i << wxT(" - ") << name << e;
      }

      s << wxT("==============================") << e;
      s << wxT("Available playback volumes:") << e;
      cnt = Px_GetNumOutputVolumes(PortMixer);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetOutputVolumeName(PortMixer, i));
         s << i << wxT(" - ") << name << e;
      }

      // Determine mixer capabilities - it it doesn't support either
      // input or output, we emulate them (by multiplying this value
      // by all incoming/outgoing samples)

      MixerOutputVol = Px_GetPCMOutputVolume(PortMixer);
      EmulateMixerOutputVol = false;
      Px_SetPCMOutputVolume(PortMixer, 0.0);
      if (Px_GetPCMOutputVolume(PortMixer) > 0.1)
         EmulateMixerOutputVol = true;
      Px_SetPCMOutputVolume(PortMixer, 0.2f);
      if (Px_GetPCMOutputVolume(PortMixer) < 0.1 ||
          Px_GetPCMOutputVolume(PortMixer) > 0.3)
         EmulateMixerOutputVol = true;
      Px_SetPCMOutputVolume(PortMixer, MixerOutputVol);

      MixerInputVol = Px_GetInputVolume(PortMixer);
      EmulateMixerInputVol = false;
      Px_SetInputVolume(PortMixer, 0.0);
      if (Px_GetInputVolume(PortMixer) > 0.1)
         EmulateMixerInputVol = true;
      Px_SetInputVolume(PortMixer, 0.2f);
      if (Px_GetInputVolume(PortMixer) < 0.1 ||
          Px_GetInputVolume(PortMixer) > 0.3)
         EmulateMixerInputVol = true;
      Px_SetInputVolume(PortMixer, MixerInputVol);

      Pa_CloseStream(stream);

      s << wxT("==============================") << e;
      s << wxT("Recording volume is ") << (EmulateMixerInputVol? wxT("emulated"): wxT("native")) << e;
      s << wxT("Playback volume is ") << (EmulateMixerOutputVol? wxT("emulated"): wxT("native")) << e;

      Px_CloseMixer(PortMixer);

      }  //end of massive if statement if a valid sample rate has been found
#endif
   return o.GetString();
}

#ifdef EXPERIMENTAL_MIDI_OUT
// FIXME: When EXPERIMENTAL_MIDI_IN is added (eventually) this should also be enabled -- Poke
wxString AudioIO::GetMidiDeviceInfo()
{
   wxStringOutputStream o;
   wxTextOutputStream s(o, wxEOL_UNIX);
   wxString e(wxT("\n"));

   if (IsStreamActive()) {
      return wxT("Stream is active ... unable to gather information.");
   }


   // XXX: May need to trap errors as with the normal device info
   int recDeviceNum = Pm_GetDefaultInputDeviceID();
   int playDeviceNum = Pm_GetDefaultOutputDeviceID();
   int cnt = Pm_CountDevices();

   wxLogDebug(wxT("PortMidi reports %d MIDI devices"), cnt);

   s << wxT("==============================") << e;
   s << wxT("Default recording device number: ") << recDeviceNum << e;
   s << wxT("Default playback device number: ") << playDeviceNum << e;

   wxString recDevice = gPrefs->Read(wxT("/MidiIO/RecordingDevice"), wxT(""));
   wxString playDevice = gPrefs->Read(wxT("/MidiIO/PlaybackDevice"), wxT(""));

   // This gets info on all available audio devices (input and output)
   if (cnt <= 0) {
      s << wxT("No devices found\n");
      return o.GetString();
   }

   for (int i = 0; i < cnt; i++) {
      s << wxT("==============================") << e;

      const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
      if (!info) {
         s << wxT("Device info unavailable for: ") << i << e;
         continue;
      }

      wxString name = wxSafeConvertMB2WX(info->name);
      wxString hostName = wxSafeConvertMB2WX(info->interf);

      s << wxT("Device ID: ") << i << e;
      s << wxT("Device name: ") << name << e;
      s << wxT("Host name: ") << hostName << e;
      s << wxT("Supports output: ") << info->output << e;
      s << wxT("Supports input: ") << info->input << e;
      s << wxT("Opened: ") << info->opened << e;

      if (name == playDevice && info->output)
         playDeviceNum = i;

      if (name == recDevice && info->input)
         recDeviceNum = i;

      // XXX: This is only done because the same was applied with PortAudio
      // If PortMidi returns -1 for the default device, use the first one
      if (recDeviceNum < 0 && info->input){
         recDeviceNum = i;
      }
      if (playDeviceNum < 0 && info->output){
         playDeviceNum = i;
      }
   }

   bool haveRecDevice = (recDeviceNum >= 0);
   bool havePlayDevice = (playDeviceNum >= 0);

   s << wxT("==============================") << e;
   if (haveRecDevice) {
      s << wxT("Selected MIDI recording device: ") << recDeviceNum << wxT(" - ") << recDevice << e;
   } else {
      s << wxT("No MIDI recording device found for '") << recDevice << wxT("'.") << e;
   }
   if (havePlayDevice) {
      s << wxT("Selected MIDI playback device: ") << playDeviceNum << wxT(" - ") << playDevice << e;
   } else {
      s << wxT("No MIDI playback device found for '") << playDevice << wxT("'.") << e;
   }

   // Mention our conditional compilation flags for Alpha only
#ifdef IS_ALPHA

   s << wxT("==============================") << e;
#ifdef EXPERIMENTAL_MIDI_OUT
   s << wxT("EXPERIMENTAL_MIDI_OUT is enabled") << e;
#else
   s << wxT("EXPERIMENTAL_MIDI_OUT is NOT enabled") << e;
#endif
#ifdef EXPERIMENTAL_MIDI_IN
   s << wxT("EXPERIMENTAL_MIDI_IN is enabled") << e;
#else
   s << wxT("EXPERIMENTAL_MIDI_IN is NOT enabled") << e;
#endif

#endif

   return o.GetString();
}
#endif

// This method is the data gateway between the audio thread (which
// communicates with the disk) and the PortAudio callback thread
// (which communicates with the audio device).
void AudioIO::FillBuffers()
{
   unsigned int i;

   auto delayedHandler = [this] ( AudacityException * pException ) {
      // In the main thread, stop recording
      // This is one place where the application handles disk
      // exhaustion exceptions from wave track operations, without rolling
      // back to the last pushed undo state.  Instead, partial recording
      // results are pushed as a NEW undo state.  For this reason, as
      // commented elsewhere, we want an exception safety guarantee for
      // the output wave tracks, after the failed append operation, that
      // the tracks remain as they were after the previous successful
      // (block-level) appends.

      // Note that the Flush in StopStream() may throw another exception,
      // but StopStream() contains that exception, and the logic in
      // AudacityException::DelayedHandlerAction prevents redundant message
      // boxes.
      StopStream();
      DefaultDelayedHandlerAction{}( pException );
   };

   if (mPlaybackTracks.size() > 0)
   {
      // Though extremely unlikely, it is possible that some buffers
      // will have more samples available than others.  This could happen
      // if we hit this code during the PortAudio callback.  To keep
      // things simple, we only write as much data as is vacant in
      // ALL buffers, and advance the global time by that much.
      // MB: subtract a few samples because the code below has rounding errors
      auto nAvailable = (int)GetCommonlyAvailPlayback() - 10;

      //
      // Don't fill the buffers at all unless we can do the
      // full mMaxPlaybackSecsToCopy.  This improves performance
      // by not always trying to process tiny chunks, eating the
      // CPU unnecessarily.
      //
      // The exception is if we're at the end of the selected
      // region - then we should just fill the buffer.
      //
      auto realTimeRemaining = mPlaybackSchedule.RealTimeRemaining();
      if (nAvailable >= (int)mPlaybackSamplesToCopy ||
          (mPlaybackSchedule.PlayingStraight() &&
           nAvailable > 0 &&
           nAvailable / mRate >= realTimeRemaining))
      {
         // Limit maximum buffer size (increases performance)
         auto available =
            std::min<size_t>( nAvailable, mPlaybackSamplesToCopy );

         // msmeyer: When playing a very short selection in looped
         // mode, the selection must be copied to the buffer multiple
         // times, to ensure, that the buffer has a reasonable size
         // This is the purpose of this loop.
         // PRL: or, when scrubbing, we may get work repeatedly from the
         // scrub queue.
         bool done = false;
         Maybe<wxMutexLocker> cleanup;
         do {
            // How many samples to produce for each channel.
            auto frames = available;
            bool progress = true;
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
            if (mPlaybackSchedule.Interactive())
               // scrubbing and play-at-speed are not limited by the real time
               // and length accumulators
               frames = limitSampleBufferSize(frames, mScrubDuration);
            else
#endif
            {
               double deltat = frames / mRate;
               if (deltat > realTimeRemaining)
               {
                  frames = realTimeRemaining * mRate;
                  // Don't fall into an infinite loop, if loop-playing a selection
                  // that is so short, it has no samples: detect that case
                  progress =
                     !(mPlaybackSchedule.Looping() &&
                       mPlaybackSchedule.mWarpedTime == 0.0 && frames == 0);
                  mPlaybackSchedule.RealTimeAdvance( realTimeRemaining );
               }
               else
                  mPlaybackSchedule.RealTimeAdvance( deltat );
               realTimeRemaining = mPlaybackSchedule.RealTimeRemaining();
            }

            if (!progress)
               frames = available;

            for (i = 0; i < mPlaybackTracks.size(); i++)
            {
               // The mixer here isn't actually mixing: it's just doing
               // resampling, format conversion, and possibly time track
               // warping
               decltype(mPlaybackMixers[i]->Process(frames))
                  processed = 0;
               samplePtr warpedSamples;
               //don't do anything if we have no length.  In particular, Process() will fail an wxAssert
               //that causes a crash since this is not the GUI thread and wxASSERT is a GUI call.

               // don't generate either if scrubbing at zero speed.
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
               const bool silent =
                  mPlaybackSchedule.Interactive() && mSilentScrub;
#else
               const bool silent = false;
#endif

               if (progress && !silent && frames > 0)
               {
                  processed = mPlaybackMixers[i]->Process(frames);
                  wxASSERT(processed <= frames);
                  warpedSamples = mPlaybackMixers[i]->GetBuffer();
                  const auto put = mPlaybackBuffers[i]->Put
                     (warpedSamples, floatSample, processed);
                  // wxASSERT(put == processed);
                  // but we can't assert in this thread
                  wxUnusedVar(put);
               }
               
               //if looping and processed is less than the full chunk/block/buffer that gets pulled from
               //other longer tracks, then we still need to advance the ring buffers or
               //we'll trip up on ourselves when we start them back up again.
               //if not looping we never start them up again, so its okay to not do anything
               // If scrubbing, we may be producing some silence.  Otherwise this should not happen,
               // but makes sure anyway that we produce equal
               // numbers of samples for all channels for this pass of the do-loop.
               if(processed < frames && !mPlaybackSchedule.PlayingStraight())
               {
                  mSilentBuf.Resize(frames, floatSample);
                  ClearSamples(mSilentBuf.ptr(), floatSample, 0, frames);
                  const auto put = mPlaybackBuffers[i]->Put
                     (mSilentBuf.ptr(), floatSample, frames - processed);
                  // wxASSERT(put == frames - processed);
                  // but we can't assert in this thread
                  wxUnusedVar(put);
               }
            }

            available -= frames;
            wxASSERT(available >= 0);

            switch (mPlaybackSchedule.mPlayMode)
            {
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
            case PlaybackSchedule::PLAY_SCRUB:
            case PlaybackSchedule::PLAY_AT_SPEED:
            {
               mScrubDuration -= frames;
               wxASSERT(mScrubDuration >= 0);
               done = (available == 0);
               if (!done && mScrubDuration <= 0)
               {
                  sampleCount startSample, endSample;
                  mScrubQueue->Transformer(startSample, endSample, mScrubDuration, cleanup);
                  if (mScrubDuration < 0)
                  {
                     // Can't play anything
                     // Stop even if we don't fill up available
                     mScrubDuration = 0;
                     done = true;
                  }
                  else
                  {
                     mSilentScrub = (endSample == startSample);
                     if (!mSilentScrub)
                     {
                        double startTime, endTime, speed;
                        startTime = startSample.as_double() / mRate;
                        endTime = endSample.as_double() / mRate;
                        auto diff = (endSample - startSample).as_long_long();
                        speed = double(std::abs(diff)) / mScrubDuration.as_double();
                        for (i = 0; i < mPlaybackTracks.size(); i++)
                           mPlaybackMixers[i]->SetTimesAndSpeed(startTime, endTime, speed);
                     }
                  }
               }
            }
               break;
#endif
            case PlaybackSchedule::PLAY_LOOPED:
            {
               done = !progress || (available == 0);
               // msmeyer: If playing looped, check if we are at the end of the buffer
               // and if yes, restart from the beginning.
               if (realTimeRemaining <= 0)
               {
                  for (i = 0; i < mPlaybackTracks.size(); i++)
                     mPlaybackMixers[i]->Restart();
                  mPlaybackSchedule.RealTimeRestart();
                  realTimeRemaining = mPlaybackSchedule.RealTimeRemaining();
               }
            }
               break;
            default:
               done = true;
               break;
            }
         } while (!done);
      }
   }  // end of playback buffering

   if (!mRecordingException &&
       mCaptureTracks.size() > 0)
      GuardedCall( [&] {
         // start record buffering
         const auto avail = GetCommonlyAvailCapture(); // samples
         const auto remainingTime =
            std::max(0.0, mRecordingSchedule.ToConsume());
         // This may be a very big double number:
         const auto remainingSamples = remainingTime * mRate;
         bool latencyCorrected = true;

         double deltat = avail / mRate;

         if (mAudioThreadShouldCallFillBuffersOnce ||
             deltat >= mMinCaptureSecsToCopy)
         {
            // Append captured samples to the end of the WaveTracks.
            // The WaveTracks have their own buffering for efficiency.
            AutoSaveFile blockFileLog;
            auto numChannels = mCaptureTracks.size();

            for( i = 0; i < numChannels; i++ )
            {
               sampleFormat trackFormat = mCaptureTracks[i]->GetSampleFormat();

               AutoSaveFile appendLog;
               size_t discarded = 0;

               if (!mRecordingSchedule.mLatencyCorrected) {
                  const auto correction = mRecordingSchedule.TotalCorrection();
                  if (correction >= 0) {
                     // Rightward shift
                     // Once only (per track per recording), insert some initial
                     // silence.
                     size_t size = floor( correction * mRate * mFactor);
                     SampleBuffer temp(size, trackFormat);
                     ClearSamples(temp.ptr(), trackFormat, 0, size);
                     mCaptureTracks[i]->Append(temp.ptr(), trackFormat,
                                               size, 1, &appendLog);
                  }
                  else {
                     // Leftward shift
                     // discard some samples from the ring buffers.
                     size_t size = floor(
                        mRecordingSchedule.ToDiscard() * mRate );

                     // The ring buffer might have grown concurrently -- don't discard more
                     // than the "avail" value noted above.
                     discarded = mCaptureBuffers[i]->Discard(std::min(avail, size));

                     if (discarded < size)
                        // We need to visit this again to complete the
                        // discarding.
                        latencyCorrected = false;
                  }
               }

               const float *pCrossfadeSrc = nullptr;
               size_t crossfadeStart = 0, totalCrossfadeLength = 0;
               if (i < mRecordingSchedule.mCrossfadeData.size())
               {
                  // Do crossfading
                  // The supplied crossfade samples are at the same rate as the track
                  const auto &data = mRecordingSchedule.mCrossfadeData[i];
                  totalCrossfadeLength = data.size();
                  if (totalCrossfadeLength) {
                     crossfadeStart =
                        floor(mRecordingSchedule.Consumed() * mCaptureTracks[i]->GetRate());
                     if (crossfadeStart < totalCrossfadeLength)
                        pCrossfadeSrc = data.data() + crossfadeStart;
                  }
               }

               wxASSERT(discarded <= avail);
               size_t toGet = avail - discarded;
               SampleBuffer temp;
               size_t size;
               sampleFormat format;
               if( mFactor == 1.0 )
               {
                  // Take captured samples directly
                  size = toGet;
                  if (pCrossfadeSrc)
                     // Change to float for crossfade calculation
                     format = floatSample;
                  else
                     format = trackFormat;
                  temp.Allocate(size, format);
                  const auto got =
                     mCaptureBuffers[i]->Get(temp.ptr(), format, toGet);
                  // wxASSERT(got == toGet);
                  // but we can't assert in this thread
                  wxUnusedVar(got);
                  if (double(size) > remainingSamples)
                     size = floor(remainingSamples);
               }
               else
               {
                  size = lrint(toGet * mFactor);
                  format = floatSample;
                  SampleBuffer temp1(toGet, floatSample);
                  temp.Allocate(size, format);
                  const auto got =
                     mCaptureBuffers[i]->Get(temp1.ptr(), floatSample, toGet);
                  // wxASSERT(got == toGet);
                  // but we can't assert in this thread
                  wxUnusedVar(got);
                  /* we are re-sampling on the fly. The last resampling call
                   * must flush any samples left in the rate conversion buffer
                   * so that they get recorded
                   */
                  if (toGet > 0 ) {
                     if (double(toGet) > remainingSamples)
                        toGet = floor(remainingSamples);
                     const auto results =
                     mResample[i]->Process(mFactor, (float *)temp1.ptr(), toGet,
                                           !IsStreamActive(), (float *)temp.ptr(), size);
                     size = results.second;
                  }
               }

               if (pCrossfadeSrc) {
                  wxASSERT(format == floatSample);
                  size_t crossfadeLength = std::min(size, totalCrossfadeLength - crossfadeStart);
                  if (crossfadeLength) {
                     auto ratio = double(crossfadeStart) / totalCrossfadeLength;
                     auto ratioStep = 1.0 / totalCrossfadeLength;
                     auto pCrossfadeDst = (float*)temp.ptr();

                     // Crossfade loop here
                     for (size_t ii = 0; ii < crossfadeLength; ++ii) {
                        *pCrossfadeDst = ratio * *pCrossfadeDst + (1.0 - ratio) * *pCrossfadeSrc;
                        ++pCrossfadeSrc, ++pCrossfadeDst;
                        ratio += ratioStep;
                     }
                  }
               }

               // Now append
               // see comment in second handler about guarantee
               mCaptureTracks[i]->Append(temp.ptr(), format,
                  size, 1,
                  &appendLog);

               if (!appendLog.IsEmpty())
               {
                  blockFileLog.StartTag(wxT("recordingrecovery"));
                  blockFileLog.WriteAttr(wxT("id"), mCaptureTracks[i]->GetAutoSaveIdent());
                  blockFileLog.WriteAttr(wxT("channel"), (int)i);
                  blockFileLog.WriteAttr(wxT("numchannels"), numChannels);
                  blockFileLog.WriteSubTree(appendLog);
                  blockFileLog.EndTag(wxT("recordingrecovery"));
               }
            } // end loop over capture channels

            // Now update the recording shedule position
            mRecordingSchedule.mPosition += avail / mRate;
            mRecordingSchedule.mLatencyCorrected = latencyCorrected;

            if (mListener && !blockFileLog.IsEmpty())
               mListener->OnAudioIONewBlockFiles(blockFileLog);
         }
         // end of record buffering
      },
      // handler
      [this] ( AudacityException *pException ) {
         if ( pException ) {
            // So that we don't attempt to fill the recording buffer again
            // before the main thread stops recording
            SetRecordingException();
            return ;
         }
         else
            // Don't want to intercept other exceptions (?)
            throw;
      },
      delayedHandler
   );
}

void AudioIO::SetListener(AudioIOListener* listener)
{
   if (IsBusy())
      return;

   mListener = listener;
}

#ifdef EXPERIMENTAL_MIDI_OUT

static Alg_update gAllNotesOff; // special event for loop ending
// the fields of this event are never used, only the address is important

double AudioIO::UncorrectedMidiEventTime()
{
   double time;
   if (mPlaybackSchedule.mTimeTrack)
      time =
         mPlaybackSchedule.RealDuration(mNextEventTime - MidiLoopOffset())
         + mPlaybackSchedule.mT0 + (mMidiLoopPasses *
                                    mPlaybackSchedule.mWarpedLength);
   else
      time = mNextEventTime;

   return time + PauseTime();
}

void AudioIO::OutputEvent()
{
   int channel = (mNextEvent->chan) & 0xF; // must be in [0..15]
   int command = -1;
   int data1 = -1;
   int data2 = -1;

   double eventTime = UncorrectedMidiEventTime();

   // 0.0005 is for rounding
   double time = eventTime + 0.0005 -
                 (mSynthLatency * 0.001);

   time += 1; // MidiTime() has a 1s offset
   // state changes have to go out without delay because the
   // midi stream time gets reset when playback starts, and
   // we don't want to leave any control changes scheduled for later
   if (time < 0 || mSendMidiState) time = 0;
   PmTimestamp timestamp = (PmTimestamp) (time * 1000); /* s to ms */

   // The special event gAllNotesOff means "end of playback, send
   // all notes off on all channels"
   if (mNextEvent == &gAllNotesOff) {
      bool looping = mPlaybackSchedule.Looping();
      AllNotesOff(looping);
      if (looping) {
         // jump back to beginning of loop
         ++mMidiLoopPasses;
         PrepareMidiIterator(false, MidiLoopOffset());
      } else {
         mNextEvent = NULL;
      }
      return;
   }

   // if mNextEvent's channel is visible, play it, visibility can
   // be updated while playing. Be careful: if we have a note-off,
   // then we must not pay attention to the channel selection
   // or mute/solo buttons because we must turn the note off
   // even if the user changed something after the note began
   // Note that because multiple tracks can output to the same
   // MIDI channels, it is not a good idea to send "All Notes Off"
   // when the user presses the mute button. We have no easy way
   // to know what notes are sounding on any given muted track, so
   // we'll just wait for the note-off events to happen.
   // Also note that note-offs are only sent when we call
   // mIterator->request_note_off(), so notes that are not played
   // will note generate random note-offs. There is the interesting
   // case that if the playback is paused, all-notes-off WILL be sent
   // and if playback resumes, the pending note-off events WILL also
   // be sent (but if that is a problem, there would also be a problem
   // in the non-pause case.
   if (((mNextEventTrack->IsVisibleChan(channel)) &&
        // only play if note is not muted:
        !((mHasSolo || mNextEventTrack->GetMute()) &&
          !mNextEventTrack->GetSolo())) ||
       (mNextEvent->is_note() && !mNextIsNoteOn)) {
      // Note event
      if (mNextEvent->is_note() && !mSendMidiState) {
         // Pitch and velocity
         data1 = mNextEvent->get_pitch();
         if (mNextIsNoteOn) {
            data2 = mNextEvent->get_loud(); // get velocity
            int offset = mNextEventTrack->GetVelocity();
            data2 += offset; // offset comes from per-track slider
            // clip velocity to insure a legal note-on value
            data2 = (data2 < 1 ? 1 : (data2 > 127 ? 127 : data2));
            // since we are going to play this note, we need to get a note_off
            mIterator->request_note_off();

#ifdef AUDIO_IO_GB_MIDI_WORKAROUND
            mPendingNotesOff.push_back(std::make_pair(channel, data1));
#endif
         }
         else {
            data2 = 0; // 0 velocity means "note off"
#ifdef AUDIO_IO_GB_MIDI_WORKAROUND
            auto end = mPendingNotesOff.end();
            auto iter = std::find(
               mPendingNotesOff.begin(), end, std::make_pair(channel, data1) );
            if (iter != end)
               mPendingNotesOff.erase(iter);
#endif
         }
         command = 0x90; // MIDI NOTE ON (or OFF when velocity == 0)
      // Update event
      } else if (mNextEvent->is_update()) {
         // this code is based on allegrosmfwr.cpp -- it could be improved
         // by comparing attribute pointers instead of string compares
         Alg_update_ptr update = (Alg_update_ptr) mNextEvent;
         const char *name = update->get_attribute();

         if (!strcmp(name, "programi")) {
            // Instrument change
            data1 = update->parameter.i;
            data2 = 0;
            command = 0xC0; // MIDI PROGRAM CHANGE
         } else if (!strncmp(name, "control", 7)) {
            // Controller change

            // The number of the controller being changed is embedded
            // in the parameter name.
            data1 = atoi(name + 7);
            // Allegro normalizes controller values
            data2 = ROUND(update->parameter.r * 127);
            command = 0xB0;
         } else if (!strcmp(name, "bendr")) {
            // Bend change

            // Reverse Allegro's post-processing of bend values
            int temp = ROUND(0x2000 * (update->parameter.r + 1));
            if (temp > 0x3fff) temp = 0x3fff; // 14 bits maximum
            if (temp < 0) temp = 0;
            data1 = temp & 0x7f; // low 7 bits
            data2 = temp >> 7;   // high 7 bits
            command = 0xE0; // MIDI PITCH BEND
         } else if (!strcmp(name, "pressurer")) {
            // Pressure change
            data1 = (int) (update->parameter.r * 127);
            if (update->get_identifier() < 0) {
               // Channel pressure
               data2 = 0;
               command = 0xD0; // MIDI CHANNEL PRESSURE
            } else {
               // Key pressure
               data2 = data1;
               data1 = update->get_identifier();
               command = 0xA0; // MIDI POLY PRESSURE
            }
         }
      }
      if (command != -1) {
         // keep track of greatest timestamp used
         if (timestamp > mMaxMidiTimestamp) {
            mMaxMidiTimestamp = timestamp;
         }
         Pm_WriteShort(mMidiStream, timestamp,
                    Pm_Message((int) (command + channel),
                                  (long) data1, (long) data2));
         /* wxPrintf("Pm_WriteShort %lx (%p) @ %d, advance %d\n",
                Pm_Message((int) (command + channel),
                           (long) data1, (long) data2),
                           mNextEvent, timestamp, timestamp - Pt_Time()); */
      }
   }
}

void AudioIO::GetNextEvent()
{
   mNextEventTrack = NULL; // clear it just to be safe
   // now get the next event and the track from which it came
   double nextOffset;
   if (!mIterator) {
        mNextEvent = NULL;
        return;
   }
   auto midiLoopOffset = MidiLoopOffset();
   mNextEvent = mIterator->next(&mNextIsNoteOn,
      (void **) &mNextEventTrack,
      &nextOffset, mPlaybackSchedule.mT1 + midiLoopOffset);

   mNextEventTime  = mPlaybackSchedule.mT1 + midiLoopOffset + 1;
   if (mNextEvent) {
      mNextEventTime = (mNextIsNoteOn ? mNextEvent->time :
                              mNextEvent->get_end_time()) + nextOffset;;
   } 
   if (mNextEventTime > (mPlaybackSchedule.mT1 + midiLoopOffset)){ // terminate playback at mT1
      mNextEvent = &gAllNotesOff;
      mNextEventTime = mPlaybackSchedule.mT1 + midiLoopOffset - ALG_EPS;
      mNextIsNoteOn = true; // do not look at duration
      mIterator->end();
      mIterator.reset(); // debugging aid
   }
}


bool AudioIO::SetHasSolo(bool hasSolo)
{
   mHasSolo = hasSolo;
   return mHasSolo;
}


void AudioIO::FillMidiBuffers()
{
   // Keep track of time paused. If not paused, fill buffers.
   if (IsPaused()) {
      if (!mMidiPaused) {
         mMidiPaused = true;
         AllNotesOff(); // to avoid hanging notes during pause
      }
      return;
   }

   if (mMidiPaused) {
      mMidiPaused = false;
   }

   bool hasSolo = false;
   auto numPlaybackTracks = mPlaybackTracks.size();
   for(unsigned t = 0; t < numPlaybackTracks; t++ )
      if( mPlaybackTracks[t]->GetSolo() ) {
         hasSolo = true;
         break;
      }
   auto numMidiPlaybackTracks = mMidiPlaybackTracks.size();
   for(unsigned t = 0; t < numMidiPlaybackTracks; t++ )
      if( mMidiPlaybackTracks[t]->GetSolo() ) {
         hasSolo = true;
         break;
      }
   SetHasSolo(hasSolo);
   // If we compute until mNextEventTime > current audio time,
   // we would have a built-in compute-ahead of mAudioOutLatency, and
   // it's probably good to compute MIDI when we compute audio (so when
   // we stop, both stop about the same time).
   double time = AudioTime(); // compute to here
   // But if mAudioOutLatency is very low, we might need some extra
   // compute-ahead to deal with mSynthLatency or even this thread.
   double actual_latency  = (MIDI_SLEEP + THREAD_LATENCY +
                             MIDI_MINIMAL_LATENCY_MS + mSynthLatency) * 0.001;
   if (actual_latency > mAudioOutLatency) {
       time += actual_latency - mAudioOutLatency;
   }
   while (mNextEvent &&
          UncorrectedMidiEventTime() < time) {
      OutputEvent();
      GetNextEvent();
   }

   // test for end
   double realTime = MidiTime() * 0.001 -
                      PauseTime();
   realTime -= 1; // MidiTime() runs ahead 1s

   // XXX Is this still true now?  It seems to break looping --Poke
   //
   // The TrackPanel::OnTimer() method updates the time position
   // indicator every 200ms, so it tends to not advance the
   // indicator to the end of the selection (mT1) but instead stop
   // up to 200ms before the end. At this point, output is shut
   // down and the indicator is removed, but for a brief time, the
   // indicator is clearly stopped before reaching mT1. To avoid
   // this, we do not set mMidiOutputComplete until we are actually
   // 0.22s beyond mT1 (even though we stop playing at mT1). This
   // gives OnTimer() time to wake up and draw the final time
   // position at mT1 before shutting down the stream.
   const double loopDelay = 0.220;

   auto timeAtSpeed = mPlaybackSchedule.TrackDuration(realTime);

   mMidiOutputComplete =
      (mPlaybackSchedule.PlayingStraight() && // PRL:  what if scrubbing?
       timeAtSpeed >= mPlaybackSchedule.mT1 + loopDelay);
   // !mNextEvent);
}

double AudioIO::PauseTime()
{
   return mNumPauseFrames / mRate;
}


// MidiTime() is an estimate in milliseconds of the current audio
// output (DAC) time + 1s. In other words, what audacity track time
// corresponds to the audio (including pause insertions) at the output?
//
PmTimestamp AudioIO::MidiTime()
{
   // note: the extra 0.0005 is for rounding. Round down by casting to
   // unsigned long, then convert to PmTimeStamp (currently signed)

   // PRL: the time correction is really Midi latency achieved by different
   // means than specifying it to Pm_OpenStream.  The use of the accumulated
   // sample count generated by the audio callback (in AudioTime()) might also
   // have the virtue of keeping the Midi output synched with audio.

   PmTimestamp ts;
   // subtract latency here because mSystemMinusAudioTime gets us
   // to the current *write* time, but we're writing ahead by audio output
   // latency (mAudioOutLatency).
   double now = SystemTime(mUsingAlsa);
   ts = (PmTimestamp) ((unsigned long)
         (1000 * (now + 1.0005 -
                  mSystemMinusAudioTimePlusLatency)));
   // wxPrintf("AudioIO::MidiTime() %d time %g sys-aud %g\n",
   //        ts, now, mSystemMinusAudioTime);
   return ts + MIDI_MINIMAL_LATENCY_MS;
}


void AudioIO::AllNotesOff(bool looping)
{
#ifdef __WXGTK__
   bool doDelay = !looping;
#else
   bool doDelay = false;
   static_cast<void>(looping);// compiler food.
#endif

   // to keep track of when MIDI should all be delivered,
   // update mMaxMidiTimestamp to now:
   PmTimestamp now = MidiTime();
   if (mMaxMidiTimestamp < now) {
       mMaxMidiTimestamp = now;
   }
#ifdef AUDIO_IO_GB_MIDI_WORKAROUND
   // PRL:
   // Send individual note-off messages for each note-on not yet paired.

   // RBD:
   // Even this did not work as planned. My guess is ALSA does not use
   // a "stable sort" for timed messages, so that when a note-off is
   // added later at the same time as a future note-on, the order is
   // not respected, and the note-off can go first, leaving a stuck note.
   // The workaround here is to use mMaxMidiTimestamp to ensure that
   // note-offs come at least 1ms later than any previous message

   // PRL:
   // I think we should do that only when stopping or pausing, not when looping
   // Note that on Linux, MIDI always uses ALSA, no matter whether portaudio
   // uses some other host api.

   mMaxMidiTimestamp += 1;
   for (const auto &pair : mPendingNotesOff) {
      Pm_WriteShort(mMidiStream,
                    (doDelay ? mMaxMidiTimestamp : 0),
                    Pm_Message(
         0x90 + pair.first, pair.second, 0));
      mMaxMidiTimestamp++; // allow 1ms per note-off
   }
   mPendingNotesOff.clear();

   // Proceed to do the usual messages too.
#endif

   for (int chan = 0; chan < 16; chan++) {
      Pm_WriteShort(mMidiStream,
                    (doDelay ? mMaxMidiTimestamp : 0),
                    Pm_Message(0xB0 + chan, 0x7B, 0));
      mMaxMidiTimestamp++; // allow 1ms per all-notes-off
   }
}

#endif

// Automated Input Level Adjustment - Automatically tries to find an acceptable input volume
#ifdef EXPERIMENTAL_AUTOMATED_INPUT_LEVEL_ADJUSTMENT
void AudioIO::AILAInitialize() {
   gPrefs->Read(wxT("/AudioIO/AutomatedInputLevelAdjustment"), &mAILAActive,         false);
   gPrefs->Read(wxT("/AudioIO/TargetPeak"),            &mAILAGoalPoint,      AILA_DEF_TARGET_PEAK);
   gPrefs->Read(wxT("/AudioIO/DeltaPeakVolume"),       &mAILAGoalDelta,      AILA_DEF_DELTA_PEAK);
   gPrefs->Read(wxT("/AudioIO/AnalysisTime"),          &mAILAAnalysisTime,   AILA_DEF_ANALYSIS_TIME);
   gPrefs->Read(wxT("/AudioIO/NumberAnalysis"),        &mAILATotalAnalysis,  AILA_DEF_NUMBER_ANALYSIS);
   mAILAGoalDelta         /= 100.0;
   mAILAGoalPoint         /= 100.0;
   mAILAAnalysisTime      /= 1000.0;
   mAILAMax                = 0.0;
   mAILALastStartTime      = max(0.0, mPlaybackSchedule.mT0);
   mAILAClipped            = false;
   mAILAAnalysisCounter    = 0;
   mAILAChangeFactor       = 1.0;
   mAILALastChangeType     = 0;
   mAILATopLevel           = 1.0;
   mAILAAnalysisEndTime    = -1.0;
}

void AudioIO::AILADisable() {
   mAILAActive = false;
}

bool AudioIO::AILAIsActive() {
   return mAILAActive;
}

void AudioIO::AILASetStartTime() {
   mAILAAbsolutStartTime = Pa_GetStreamTime(mPortStreamV19);
   wxPrintf("START TIME %f\n\n", mAILAAbsolutStartTime);
}

double AudioIO::AILAGetLastDecisionTime() {
   return mAILAAnalysisEndTime;
}

void AudioIO::AILAProcess(double maxPeak) {
   AudacityProject *const proj = mOwningProject);
   if (proj && mAILAActive) {
      if (mInputMeter && mInputMeter->IsClipping()) {
         mAILAClipped = true;
         wxPrintf("clipped");
      }

      mAILAMax = max(mAILAMax, maxPeak);

      if ((mAILATotalAnalysis == 0 || mAILAAnalysisCounter < mAILATotalAnalysis) && mPlaybackSchedule.GetTrackTime() - mAILALastStartTime >= mAILAAnalysisTime) {
         auto ToLinearIfDB = [](double value, int dbRange) {
            if (dbRange >= 0)
               value = pow(10.0, (-(1.0-value) * dbRange)/20.0);
            return value;
         };

         putchar('\n');
         mAILAMax = mInputMeter ? ToLinearIfDB(mAILAMax, mInputMeter->GetDBRange()) : 0.0;
         double iv = (double) Px_GetInputVolume(mPortMixer);
         unsigned short changetype = 0; //0 - no change, 1 - increase change, 2 - decrease change
         wxPrintf("mAILAAnalysisCounter:%d\n", mAILAAnalysisCounter);
         wxPrintf("\tmAILAClipped:%d\n", mAILAClipped);
         wxPrintf("\tmAILAMax (linear):%f\n", mAILAMax);
         wxPrintf("\tmAILAGoalPoint:%f\n", mAILAGoalPoint);
         wxPrintf("\tmAILAGoalDelta:%f\n", mAILAGoalDelta);
         wxPrintf("\tiv:%f\n", iv);
         wxPrintf("\tmAILAChangeFactor:%f\n", mAILAChangeFactor);
         if (mAILAClipped || mAILAMax > mAILAGoalPoint + mAILAGoalDelta) {
            wxPrintf("too high:\n");
            mAILATopLevel = min(mAILATopLevel, iv);
            wxPrintf("\tmAILATopLevel:%f\n", mAILATopLevel);
            //if clipped or too high
            if (iv <= LOWER_BOUND) {
               //we can't improve it more now
               if (mAILATotalAnalysis != 0) {
                  mAILAActive = false;
                  proj->TP_DisplayStatusMessage(_("Automated Recording Level Adjustment stopped. It was not possible to optimize it more. Still too high."));
               }
               wxPrintf("\talready min vol:%f\n", iv);
            }
            else {
               float vol = (float) max(LOWER_BOUND, iv+(mAILAGoalPoint-mAILAMax)*mAILAChangeFactor);
               Px_SetInputVolume(mPortMixer, vol);
               wxString msg;
               msg.Printf(_("Automated Recording Level Adjustment decreased the volume to %f."), vol);
               proj->TP_DisplayStatusMessage(msg);
               changetype = 1;
               wxPrintf("\tnew vol:%f\n", vol);
               float check = Px_GetInputVolume(mPortMixer);
               wxPrintf("\tverified %f\n", check);
            }
         }
         else if ( mAILAMax < mAILAGoalPoint - mAILAGoalDelta ) {
            //if too low
            wxPrintf("too low:\n");
            if (iv >= UPPER_BOUND || iv + 0.005 > mAILATopLevel) { //condition for too low volumes and/or variable volumes that cause mAILATopLevel to decrease too much
               //we can't improve it more
               if (mAILATotalAnalysis != 0) {
                  mAILAActive = false;
                  proj->TP_DisplayStatusMessage(_("Automated Recording Level Adjustment stopped. It was not possible to optimize it more. Still too low."));
               }
               wxPrintf("\talready max vol:%f\n", iv);
            }
            else {
               float vol = (float) min(UPPER_BOUND, iv+(mAILAGoalPoint-mAILAMax)*mAILAChangeFactor);
               if (vol > mAILATopLevel) {
                  vol = (iv + mAILATopLevel)/2.0;
                  wxPrintf("\tTruncated vol:%f\n", vol);
               }
               Px_SetInputVolume(mPortMixer, vol);
               wxString msg;
               msg.Printf(_("Automated Recording Level Adjustment increased the volume to %.2f."), vol);
               proj->TP_DisplayStatusMessage(msg);
               changetype = 2;
               wxPrintf("\tnew vol:%f\n", vol);
               float check = Px_GetInputVolume(mPortMixer);
               wxPrintf("\tverified %f\n", check);
            }
         }

         mAILAAnalysisCounter++;
         //const PaStreamInfo* info = Pa_GetStreamInfo(mPortStreamV19);
         //double latency = 0.0;
         //if (info)
         //   latency = info->inputLatency;
         //mAILAAnalysisEndTime = mTime+latency;
         mAILAAnalysisEndTime = Pa_GetStreamTime(mPortStreamV19) - mAILAAbsolutStartTime;
         mAILAMax             = 0;
         wxPrintf("\tA decision was made @ %f\n", mAILAAnalysisEndTime);
         mAILAClipped         = false;
         mAILALastStartTime   = mPlaybackSchedule.GetTrackTime();

         if (changetype == 0)
            mAILAChangeFactor *= 0.8; //time factor
         else if (mAILALastChangeType == changetype)
            mAILAChangeFactor *= 1.1; //concordance factor
         else
            mAILAChangeFactor *= 0.7; //discordance factor
         mAILALastChangeType = changetype;
         putchar('\n');
      }

      if (mAILAActive && mAILATotalAnalysis != 0 && mAILAAnalysisCounter >= mAILATotalAnalysis) {
         mAILAActive = false;
         if (mAILAMax > mAILAGoalPoint + mAILAGoalDelta)
            proj->TP_DisplayStatusMessage(_("Automated Recording Level Adjustment stopped. The total number of analyses has been exceeded without finding an acceptable volume. Still too high."));
         else if (mAILAMax < mAILAGoalPoint - mAILAGoalDelta)
            proj->TP_DisplayStatusMessage(_("Automated Recording Level Adjustment stopped. The total number of analyses has been exceeded without finding an acceptable volume. Still too low."));
         else {
            wxString msg;
            msg.Printf(_("Automated Recording Level Adjustment stopped. %.2f seems an acceptable volume."), Px_GetInputVolume(mPortMixer));
            proj->TP_DisplayStatusMessage(msg);
         }
      }
   }
}
#endif

//////////////////////////////////////////////////////////////////////
//
//    PortAudio callback thread context
//
//////////////////////////////////////////////////////////////////////

#define MAX(a,b) ((a) > (b) ? (a) : (b))

static void DoSoftwarePlaythrough(const void *inputBuffer,
                                  sampleFormat inputFormat,
                                  unsigned inputChannels,
                                  float *outputBuffer,
                                  int len)
{
   for (unsigned int i=0; i < inputChannels; i++) {
      samplePtr inputPtr = ((samplePtr)inputBuffer) + (i * SAMPLE_SIZE(inputFormat));
      samplePtr outputPtr = ((samplePtr)outputBuffer) + (i * SAMPLE_SIZE(floatSample));

      CopySamples(inputPtr, inputFormat,
                  (samplePtr)outputPtr, floatSample,
                  len, true, inputChannels, 2);
   }

   // One mono input channel goes to both output channels...
   if (inputChannels == 1)
      for (int i=0; i < len; i++)
         outputBuffer[2*i + 1] = outputBuffer[2*i];
}

int audacityAudioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          const PaStreamCallbackFlags statusFlags, void *userData )
{
   return gAudioIO->AudioCallback(
      inputBuffer, outputBuffer, framesPerBuffer,
      timeInfo, statusFlags, userData);
}

int AudioIO::AudioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
#ifdef EXPERIMENTAL_MIDI_OUT
                          const PaStreamCallbackTimeInfo *timeInfo,
#else
                          const PaStreamCallbackTimeInfo * WXUNUSED(timeInfo),
#endif
                          const PaStreamCallbackFlags statusFlags, void * WXUNUSED(userData) )
{
   const auto numPlaybackChannels = mNumPlaybackChannels;
   const auto numPlaybackTracks = mPlaybackTracks.size();
   const auto numCaptureChannels = mNumCaptureChannels;
   int callbackReturn = paContinue;
   void *tempBuffer = alloca(framesPerBuffer*sizeof(float)*
                             MAX(numCaptureChannels,numPlaybackChannels));
   float *tempFloats = (float*)tempBuffer;

   // output meter may need samples untouched by volume emulation
   float *outputMeterFloats;
   outputMeterFloats =
      (outputBuffer && mEmulateMixerOutputVol &&
                       mMixerOutputVol != 1.0) ?
         (float *)alloca(framesPerBuffer*numPlaybackChannels * sizeof(float)) :
         (float *)outputBuffer;

#ifdef EXPERIMENTAL_MIDI_OUT
   if (mCallbackCount++ == 0) {
       // This is effectively mSystemMinusAudioTime when the buffer is empty:
       mStartTime = SystemTime(mUsingAlsa) - mPlaybackSchedule.mT0;
       // later, mStartTime - mSystemMinusAudioTime will tell us latency
   }

   /* GSW: Save timeInfo in case MidiPlayback needs it */
   mAudioCallbackClockTime = PaUtil_GetTime();

   /* for Linux, estimate a smooth audio time as a slowly-changing
      offset from system time */
   // rnow is system time as a double to simplify math
   double rnow = SystemTime(mUsingAlsa);
   // anow is next-sample-to-be-computed audio time as a double
   double anow = AudioTime();

   if (mUsingAlsa) {
      // timeInfo's fields are not all reliable.

      // enow is audio time estimated from our clock synchronization protocol,
      //   which produces mSystemMinusAudioTime. But we want the estimate
      //   to drift low, so we steadily increase mSystemMinusAudioTime to
      //   simulate a fast system clock or a slow audio clock. If anow > enow,
      //   we'll update mSystemMinusAudioTime to keep in sync. (You might think
      //   we could just use anow as the "truth", but it has a lot of jitter,
      //   so we are using enow to smooth out this jitter, in fact to < 1ms.)
      // Add worst-case clock drift using previous framesPerBuffer:
      const auto increase =
         mAudioFramesPerBuffer * 0.0002 / mRate;
      mSystemMinusAudioTime += increase;
      mSystemMinusAudioTimePlusLatency += increase;
      double enow = rnow - mSystemMinusAudioTime;


      // now, use anow instead if it is ahead of enow
      if (anow > enow) {
         mSystemMinusAudioTime = rnow - anow;
         // Update our mAudioOutLatency estimate during the first 20 callbacks.
         // During this period, the buffer should fill. Once we have a good
         // estimate of mSystemMinusAudioTime (expected in fewer than 20 callbacks)
         // we want to stop the updating in case there is clock drift, which would
         // cause the mAudioOutLatency estimation to drift as well. The clock drift
         // in the first 20 callbacks should be negligible, however.
         if (mCallbackCount < 20) {
            mAudioOutLatency = mStartTime -
               mSystemMinusAudioTime;
         }
         mSystemMinusAudioTimePlusLatency =
            mSystemMinusAudioTime + mAudioOutLatency;
      }
   }
   else {
      // If not using Alsa, rely on timeInfo to have meaningful values that are
      // more precise than the output latency value reported at stream start.
      mSystemMinusAudioTime = rnow - anow;
      mSystemMinusAudioTimePlusLatency =
         mSystemMinusAudioTime +
            (timeInfo->outputBufferDacTime - timeInfo->currentTime);
   }

   mAudioFramesPerBuffer = framesPerBuffer;
   if (IsPaused()
       // PRL:  Why was this added?  Was it only because of the mysterious
       // initial leading zeroes, now solved by setting mStreamToken early?
       || mStreamToken <= 0
       )
      mNumPauseFrames += framesPerBuffer;

   // PRL:  Note that when there is a separate MIDI thread, it is effectively
   // blocked until the first visit to this line during a playback, and will
   // not read mSystemMinusAudioTimePlusLatency sooner:
   mNumFrames += framesPerBuffer;

#ifndef USE_MIDI_THREAD
   if (mMidiStream)
      FillMidiBuffers();
#endif

#endif

   unsigned int i;

   /* Send data to recording VU meter if applicable */

   if (mInputMeter &&
         !mInputMeter->IsMeterDisabled() &&
         inputBuffer) {
      // get here if meters are actually live , and being updated
      /* It's critical that we don't update the meters while StopStream is
       * trying to stop PortAudio, otherwise it can lead to a freeze.  We use
       * two variables to synchronize:
       *   mUpdatingMeters tells StopStream when the callback is about to enter
       *     the code where it might update the meters, and
       *   mUpdateMeters is how the rest of the code tells the callback when it
       *     is allowed to actually do the updating.
       * Note that mUpdatingMeters must be set first to avoid a race condition.
       */
      mUpdatingMeters = true;
      if (mUpdateMeters) {
         if (mCaptureFormat == floatSample)
            mInputMeter->UpdateDisplay(numCaptureChannels,
                                                 framesPerBuffer,
                                                 (float *)inputBuffer);
         else {
            CopySamples((samplePtr)inputBuffer, mCaptureFormat,
                        (samplePtr)tempFloats, floatSample,
                        framesPerBuffer * numCaptureChannels);
            mInputMeter->UpdateDisplay(numCaptureChannels,
                                                 framesPerBuffer,
                                                 tempFloats);
         }
      }
      mUpdatingMeters = false;
   }  // end recording VU meter update

   // Stop recording if 'silence' is detected
   //
   // LL:  We'd gotten a little "dangerous" with the control toolbar calls
   //      here because we are not running in the main GUI thread.  Eventually
   //      the toolbar attempts to update the active project's status bar.
   //      But, since we're not in the main thread, we can get all manner of
   //      really weird failures.  Or none at all which is even worse, since
   //      we don't know a problem exists.
   //
   //      By using CallAfter(), we can schedule the call to the toolbar
   //      to run in the main GUI thread after the next event loop iteration.
   if(mPauseRec && inputBuffer && mInputMeter) {
      if(mInputMeter->GetMaxPeak() < mSilenceLevel ) {
         if(!IsPaused()) {
            ControlToolBar *bar = mOwningProject->GetControlToolBar();
            bar->CallAfter(&ControlToolBar::Pause);
         }
      }
      else {
         if(IsPaused()) {
            ControlToolBar *bar = mOwningProject->GetControlToolBar();
            bar->CallAfter(&ControlToolBar::Pause);
         }
      }
   }
   if( mPaused )
   {
      if (outputBuffer && numPlaybackChannels > 0)
      {
         ClearSamples((samplePtr)outputBuffer, floatSample,
                      0, framesPerBuffer * numPlaybackChannels);

         if (inputBuffer && mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }
      }

      return paContinue;
   }

   if (mStreamToken > 0)
   {
      //
      // Mix and copy to PortAudio's output buffer
      //

      if( outputBuffer && (numPlaybackChannels > 0) )
      {
         bool cut = false;
         bool linkFlag = false;

         float *outputFloats = (float *)outputBuffer;
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
            outputFloats[i] = 0.0;

         if (inputBuffer && mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }

         // Copy the results to outputMeterFloats if necessary
         if (outputMeterFloats != outputFloats) {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i) {
               outputMeterFloats[i] = outputFloats[i];
            }
         }

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
         // While scrubbing, ignore seek requests
         if (mSeek && mPlaybackSchedule.Interactive())
            mSeek = 0.0;
         else
#endif
         if (mSeek)
            return CallbackDoSeek();

         unsigned numSolo = 0;
         for(unsigned t = 0; t < numPlaybackTracks; t++ )
            if( mPlaybackTracks[t]->GetSolo() )
               numSolo++;
#ifdef EXPERIMENTAL_MIDI_OUT
         auto numMidiPlaybackTracks = mMidiPlaybackTracks.size();
         for( unsigned t = 0; t < numMidiPlaybackTracks; t++ )
            if( mMidiPlaybackTracks[t]->GetSolo() )
               numSolo++;
#endif

         const WaveTrack **chans = (const WaveTrack **) alloca(numPlaybackChannels * sizeof(WaveTrack *));
         float **tempBufs = (float **) alloca(numPlaybackChannels * sizeof(float *));
         for (unsigned int c = 0; c < numPlaybackChannels; c++)
         {
            tempBufs[c] = (float *) alloca(framesPerBuffer * sizeof(float));
         }

         EffectManager & em = EffectManager::Get();
         em.RealtimeProcessStart();

         bool selected = false;
         int group = 0;
         int chanCnt = 0;
         decltype(framesPerBuffer) maxLen = 0;
         for (unsigned t = 0; t < numPlaybackTracks; t++)
         {
            const WaveTrack *vt = mPlaybackTracks[t].get();

            chans[chanCnt] = vt;

            if (linkFlag)
               linkFlag = false;
            else {
               cut = false;

               // Cut if somebody else is soloing
               if (numSolo>0 && !vt->GetSolo())
                  cut = true;

               // Cut if we're muted (unless we're soloing)
               if (vt->GetMute() && !vt->GetSolo())
                  cut = true;

               linkFlag = vt->GetLinked();
               selected = vt->GetSelected();

               // If we have a mono track, clear the right channel
               if (!linkFlag)
               {
                  memset(tempBufs[1], 0, framesPerBuffer * sizeof(float));
               }
            }

#define ORIGINAL_DO_NOT_PLAY_ALL_MUTED_TRACKS_TO_END
#ifdef ORIGINAL_DO_NOT_PLAY_ALL_MUTED_TRACKS_TO_END
            decltype(framesPerBuffer) len = 0;
            // this is original code prior to r10680 -RBD
            if (cut)
            {
               len = mPlaybackBuffers[t]->Discard(framesPerBuffer);
               // keep going here.  
               // we may still need to issue a paComplete.
            }
            else
            {
               len = mPlaybackBuffers[t]->Get((samplePtr)tempBufs[chanCnt],
                                                         floatSample,
                                                         framesPerBuffer);
               if (len < framesPerBuffer)
                  // Pad with zeroes to the end, in case of a short channel
                  memset((void*)&tempBufs[chanCnt][len], 0,
                     (framesPerBuffer - len) * sizeof(float));

               chanCnt++;
            }

            // PRL:  Bug1104:
            // There can be a difference of len in different loop passes if one channel
            // of a stereo track ends before the other!  Take a max!
            maxLen = std::max(maxLen, len);


            if (linkFlag)
            {
               continue;
            }
#else
            // This code was reorganized so that if all audio tracks
            // are muted, we still return paComplete when the end of
            // a selection is reached.
            // Vaughan, 2011-10-20: Further comments from Roger, by off-list email:
            //    ...something to do with what it means to mute all audio tracks. E.g. if you
            // mute all and play, does the playback terminate immediately or play
            // silence? If it terminates immediately, does that terminate any MIDI
            // playback that might also be going on? ...Maybe muted audio tracks + MIDI,
            // the playback would NEVER terminate. ...I think the #else part is probably preferable...
            size_t len;
            if (cut)
            {
               len =
                  mPlaybackBuffers[t]->Discard(framesPerBuffer);
            } else
            {
               len =
                  mPlaybackBuffers[t]->Get((samplePtr)tempFloats,
                                                     floatSample,
                                                     framesPerBuffer);
            }
#endif

            // Last channel seen now
            len = maxLen;

            if( !cut && selected )
            {
               len = em.RealtimeProcess(group, chanCnt, tempBufs, len);
            }
            group++;


            CallbackCheckCompletion(callbackReturn, len);

            if (cut) // no samples to process, they've been discarded
               continue;

            for (int c = 0; c < chanCnt; c++)
            {
               vt = chans[c];

               if (vt->GetChannel() == Track::LeftChannel ||
                   vt->GetChannel() == Track::MonoChannel)
               {
                  float gain = vt->GetChannelGain(0);

                  // Output volume emulation: possibly copy meter samples, then
                  // apply volume, then copy to the output buffer
                  if (outputMeterFloats != outputFloats)
                     for (decltype(len) i = 0; i < len; ++i)
                        outputMeterFloats[numPlaybackChannels*i] +=
                           gain*tempFloats[i];

                  if (mEmulateMixerOutputVol)
                     gain *= mMixerOutputVol;

                  for(decltype(len) i = 0; i < len; i++)
                     outputFloats[numPlaybackChannels*i] += gain*tempBufs[c][i];
               }

               if (vt->GetChannel() == Track::RightChannel ||
                   vt->GetChannel() == Track::MonoChannel)
               {
                  float gain = vt->GetChannelGain(1);

                  // Output volume emulation (as above)
                  if (outputMeterFloats != outputFloats)
                     for (decltype(len) i = 0; i < len; ++i)
                        outputMeterFloats[numPlaybackChannels*i+1] +=
                           gain*tempFloats[i];

                  if (mEmulateMixerOutputVol)
                     gain *= mMixerOutputVol;

                  for(decltype(len) i = 0; i < len; i++)
                     outputFloats[numPlaybackChannels*i+1] += gain*tempBufs[c][i];
               }
            }

            chanCnt = 0;
         }
         // Poke: If there are no playback tracks, then the earlier check
         // about the time indicator being passed the end won't happen;
         // do it here instead (but not if looping or scrubbing)
         if (numPlaybackTracks == 0)
            CallbackCheckCompletion(callbackReturn, 0);

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
         // Update the current time position, for scrubbing
         // "Consume" only as much as the ring buffers produced, which may
         // be less than framesPerBuffer (during "stutter")
         if (mPlaybackSchedule.Interactive())
            mPlaybackSchedule.SetTrackTime( mScrubQueue->Consumer( maxLen ) );
#endif

         em.RealtimeProcessEnd();

         mLastPlaybackTimeMillis = ::wxGetLocalTimeMillis();

         //
         // Clip output to [-1.0,+1.0] range (msmeyer)
         //
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
         {
            float f = outputFloats[i];
            if (f > 1.0)
               outputFloats[i] = 1.0;
            else if (f < -1.0)
               outputFloats[i] = -1.0;
         }

         // Same for meter output
         if (outputMeterFloats != outputFloats)
         {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i)
            {
               float f = outputMeterFloats[i];
               if (f > 1.0)
                  outputMeterFloats[i] = 1.0;
               else if (f < -1.0)
                  outputMeterFloats[i] = -1.0;
            }
         }
      }

      //
      // Copy from PortAudio to our input buffers.
      //

      if( inputBuffer && (numCaptureChannels > 0) )
      {
         // If there are no playback tracks, and we are recording, then the
         // earlier checks for being passed the end won't happen, so do it here.
         if (mPlaybackSchedule.PassIsComplete()) {
            callbackReturn = paComplete;
         }

         // The error likely from a too-busy CPU falling behind real-time data
         // is paInputOverflow
         bool inputError =
            (statusFlags & (paInputOverflow))
            && !(statusFlags & paPrimingOutput);

         // But it seems it's easy to get false positives, at least on Mac
         // So we have not decided to enable this extra detection yet in
         // production

         size_t len = framesPerBuffer;
         for(unsigned t = 0; t < numCaptureChannels; t++)
            len = std::min( len,
                           mCaptureBuffers[t]->AvailForPut());

         if (mSimulateRecordingErrors && 100LL * rand() < RAND_MAX)
            // Make spurious errors for purposes of testing the error
            // reporting
            len = 0;

         // A different symptom is that len < framesPerBuffer because
         // the other thread, executing FillBuffers, isn't consuming fast
         // enough from mCaptureBuffers; maybe it's CPU-bound, or maybe the
         // storage device it writes is too slow
         if (mDetectDropouts &&
             ((mDetectUpstreamDropouts && inputError) ||
              len < framesPerBuffer) ) {
            // Assume that any good partial buffer should be written leftmost
            // and zeroes will be padded after; label the zeroes.
            auto start = mPlaybackSchedule.GetTrackTime() +
                len / mRate + mRecordingSchedule.mLatencyCorrection;
            auto duration = (framesPerBuffer - len) / mRate;
            auto interval = std::make_pair( start, duration );
            mLostCaptureIntervals.push_back( interval );
         }

         if (len < framesPerBuffer)
         {
            mLostSamples += (framesPerBuffer - len);
            wxPrintf(wxT("lost %d samples\n"), (int)(framesPerBuffer - len));
         }

         if (len > 0) {
            for(unsigned t = 0; t < numCaptureChannels; t++) {

               // dmazzoni:
               // Un-interleave.  Ugly special-case code required because the
               // capture channels could be in three different sample formats;
               // it'd be nice to be able to call CopySamples, but it can't
               // handle multiplying by the gain and then clipping.  Bummer.

               switch(mCaptureFormat) {
               case floatSample: {
                  float *inputFloats = (float *)inputBuffer;
                  for( i = 0; i < len; i++)
                     tempFloats[i] =
                        inputFloats[numCaptureChannels*i+t];
               } break;
               case int24Sample:
                  // We should never get here. Audacity's int24Sample format
                  // is different from PortAudio's sample format and so we
                  // make PortAudio return float samples when recording in
                  // 24-bit samples.
                  wxASSERT(false);
                  break;
               case int16Sample: {
                  short *inputShorts = (short *)inputBuffer;
                  short *tempShorts = (short *)tempBuffer;
                  for( i = 0; i < len; i++) {
                     float tmp = inputShorts[numCaptureChannels*i+t];
                     if (tmp > 32767)
                        tmp = 32767;
                     if (tmp < -32768)
                        tmp = -32768;
                     tempShorts[i] = (short)(tmp);
                  }
               } break;
               } // switch

               const auto put =
                  mCaptureBuffers[t]->Put(
                     (samplePtr)tempBuffer, mCaptureFormat, len);
               // wxASSERT(put == len);
               // but we can't assert in this thread
               wxUnusedVar(put);
            }
         }
      }

      // Update the current time position if not scrubbing
      // (Already did it above, for scrubbing)

      mPlaybackSchedule.TrackTimeUpdate( framesPerBuffer / mRate );

      // Record the reported latency from PortAudio.
      // TODO: Don't recalculate this with every callback?

      // 01/21/2009:  Disabled until a better solution presents itself.
     #if 0
      // As of 06/17/2006, portaudio v19 returns inputBufferAdcTime set to
      // zero.  It is being worked on, but for now we just can't do much
      // but follow the leader.
      //
      // 08/27/2006: too inconsistent for now...just leave it a zero.
      //
      // 04/16/2008: Looks like si->inputLatency comes back with something useful though.
      // This rearranged logic uses si->inputLatency, but if PortAudio fixes inputBufferAdcTime,
      // this code won't have to be modified to use it.
      // Also avoids setting mLastRecordingOffset except when simultaneously playing and recording.
      //
      if (numCaptureChannels > 0 && numPlaybackChannels > 0) // simultaneously playing and recording
      {
         if (timeInfo->inputBufferAdcTime > 0)
            mLastRecordingOffset = timeInfo->inputBufferAdcTime - timeInfo->outputBufferDacTime;
         else if (mLastRecordingOffset == 0.0)
         {
            const PaStreamInfo* si = Pa_GetStreamInfo( mPortStreamV19 );
            mLastRecordingOffset = -si->inputLatency;
         }
      }
     #endif
   } // if mStreamToken > 0
   else {
      // No tracks to play, but we should clear the output, and
      // possibly do software playthrough...

      if( outputBuffer && (numPlaybackChannels > 0) ) {
         float *outputFloats = (float *)outputBuffer;
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
            outputFloats[i] = 0.0;

         if (inputBuffer && mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }

         // Copy the results to outputMeterFloats if necessary
         if (outputMeterFloats != outputFloats) {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i) {
               outputMeterFloats[i] = outputFloats[i];
            }
         }
      }

   }
   /* Send data to playback VU meter if applicable */
   if (mOutputMeter &&
      !mOutputMeter->IsMeterDisabled() &&
      outputMeterFloats) {
      // Get here if playback meter is live
      /* It's critical that we don't update the meters while StopStream is
       * trying to stop PortAudio, otherwise it can lead to a freeze.  We use
       * two variables to synchronize:
       *  mUpdatingMeters tells StopStream when the callback is about to enter
       *    the code where it might update the meters, and
       *  mUpdateMeters is how the rest of the code tells the callback when it
       *    is allowed to actually do the updating.
       * Note that mUpdatingMeters must be set first to avoid a race condition.
       */
      mUpdatingMeters = true;
      if (mUpdateMeters) {
         mOutputMeter->UpdateDisplay(numPlaybackChannels,
                                               framesPerBuffer,
                                               outputMeterFloats);

         //v Vaughan, 2011-02-25: Moved this update back to TrackPanel::OnTimer()
         //    as it helps with playback issues reported by Bill and noted on Bug 258.
         //    The problem there occurs if Software Playthrough is on.
         //    Could conditionally do the update here if Software Playthrough is off,
         //    and in TrackPanel::OnTimer() if Software Playthrough is on, but not now.
         // PRL 12 Jul 2015: and what was in TrackPanel::OnTimer is now handled by means of event
         // type EVT_TRACK_PANEL_TIMER
         //MixerBoard* pMixerBoard = mOwningProject->GetMixerBoard();
         //if (pMixerBoard)
         //   pMixerBoard->UpdateMeters(GetStreamTime(),
         //                              (pProj->mLastPlayMode == loopedPlay));
      }
      mUpdatingMeters = false;
   }  // end playback VU meter update

   return callbackReturn;
}

PaStreamCallbackResult AudioIO::CallbackDoSeek()
{
   const int token = mStreamToken;
   wxMutexLocker locker(mSuspendAudioThread);
   if (token != mStreamToken)
      // This stream got destroyed while we waited for it
      return paAbort;

   const auto numPlaybackTracks = mPlaybackTracks.size();

   // Pause audio thread and wait for it to finish
   mAudioThreadFillBuffersLoopRunning = false;
   while( mAudioThreadFillBuffersLoopActive )
   {
      wxMilliSleep( 50 );
   }

   // Calculate the NEW time position, in the PortAudio callback
   const auto time = mPlaybackSchedule.ClampTrackTime(
      mPlaybackSchedule.GetTrackTime() + mSeek );
   mPlaybackSchedule.SetTrackTime( time );
   mSeek = 0.0;

   mPlaybackSchedule.RealTimeInit( time );

   // Reset mixer positions and flush buffers for all tracks
   for (size_t i = 0; i < numPlaybackTracks; i++)
   {
      mPlaybackMixers[i]->Reposition( time );
      const auto toDiscard =
         mPlaybackBuffers[i]->AvailForGet();
      const auto discarded =
         mPlaybackBuffers[i]->Discard( toDiscard );
      // wxASSERT( discarded == toDiscard );
      // but we can't assert in this thread
      wxUnusedVar(discarded);
   }

   // Reload the ring buffers
   mAudioThreadShouldCallFillBuffersOnce = true;
   while( mAudioThreadShouldCallFillBuffersOnce )
   {
      wxMilliSleep( 50 );
   }

   // Reenable the audio thread
   mAudioThreadFillBuffersLoopRunning = true;

   return paContinue;
}

void AudioIO::CallbackCheckCompletion(
   int &callbackReturn, unsigned long len)
{
   bool done = mPlaybackSchedule.PassIsComplete();
   if (done)
      done = (
         mPlaybackSchedule.PlayingAtSpeed()
         // some leftover length allowed in this case
         || (mPlaybackSchedule.PlayingStraight() && len == 0)
      );
   if(done) {
      // PRL: singalling MIDI output complete is necessary if
      // not USE_MIDI_THREAD, otherwise it's harmlessly redundant
#ifdef EXPERIMENTAL_MIDI_OUT
      mMidiOutputComplete = true,
#endif
      callbackReturn = paComplete;
   }
}

bool AudioIO::PlaybackSchedule::PassIsComplete() const
{
   // Test mTime within the PortAudio callback
   if (Scrubbing())
      return false; // but may be true if playing at speed
   return Overruns( GetTrackTime() );
}

bool AudioIO::PlaybackSchedule::Overruns( double trackTime ) const
{
   return (ReversedTime() ? trackTime <= mT1 : trackTime >= mT1);
}

void AudioIO::PlaybackSchedule::TrackTimeUpdate(double realElapsed)
{
   // Update mTime within the PortAudio callback

   if (Interactive())
      return;

   if (ReversedTime())
      realElapsed *= -1.0;

   auto time = GetTrackTime();
   if (mTimeTrack) {
      // Defense against a case that might cause the do-loop not to terminate
      if ( fabs(mT0 - mT1) < 1e-9 ) {
         SetTrackTime( mT0 );
         return;
      }

      double total;
      bool foundTotal = false;
      do {
         auto oldTime = time;
         if (foundTotal && fabs(realElapsed) > fabs(total))
            // Avoid SolveWarpedLength
            time = mT1;
         else
            time = mTimeTrack->SolveWarpedLength(time, realElapsed);
         if (Looping() && Overruns( time )) {
            // Bug1922:  The part of the time track outside the loop should not
            // influence the result
            double delta;
            if (foundTotal && oldTime == mT0)
               // Avoid integrating again
               delta = total;
            else {
               delta = mTimeTrack->ComputeWarpedLength(oldTime, mT1);
               if (oldTime == mT0)
                  foundTotal = true, total = delta;
            }
            realElapsed -= delta;
            time = mT0;
         }
         else
            break;
      } while ( true );
   }
   else {
      time += realElapsed;

      // Wrap to start if looping
      if (Looping()) {
         while ( Overruns( time ) ) {
            // LL:  This is not exactly right, but I'm at my wits end trying to
            //      figure it out.  Feel free to fix it.  :-)
            // MB: it's much easier than you think, mTime isn't warped at all!
            time -= mT1 - mT0;
         }
      }
   }
   SetTrackTime( time );
}

double AudioIO::PlaybackSchedule::TrackDuration(double realElapsed) const
{
   if (mTimeTrack)
      return mTimeTrack->SolveWarpedLength(mT0, realElapsed);
   else
      return realElapsed;
}

double AudioIO::PlaybackSchedule::RealDuration(double trackTime1) const
{
   double duration;
   if (mTimeTrack)
       duration = mTimeTrack->ComputeWarpedLength(mT0, trackTime1);
   else
      duration = trackTime1 - mT0;
   return fabs(duration);
}

double AudioIO::PlaybackSchedule::RealTimeRemaining() const
{
   return mWarpedLength - mWarpedTime;
}

void AudioIO::PlaybackSchedule::RealTimeAdvance( double increment )
{
   mWarpedTime += increment;
}

void AudioIO::PlaybackSchedule::RealTimeInit( double trackTime )
{
   if (Scrubbing())
      mWarpedTime = 0.0;
   else
      mWarpedTime = RealDuration( trackTime );
}

void AudioIO::PlaybackSchedule::RealTimeRestart()
{
   mWarpedTime = 0;
}

double AudioIO::RecordingSchedule::ToConsume() const
{
   return mDuration - Consumed();
}

double AudioIO::RecordingSchedule::Consumed() const
{
   return std::max( 0.0, mPosition + TotalCorrection() );
}

double AudioIO::RecordingSchedule::ToDiscard() const
{
   return std::max(0.0, -( mPosition + TotalCorrection() ) );
}

bool AudioIO::IsCapturing() const
{
   // Includes a test of mTime, used in the main thread
   return IsStreamActive() &&
      GetNumCaptureChannels() > 0 &&
      mPlaybackSchedule.GetTrackTime() >=
         mPlaybackSchedule.mT0 + mRecordingSchedule.mPreRoll;
}
