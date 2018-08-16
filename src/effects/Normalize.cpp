/**********************************************************************

  Audacity: A Digital Audio Editor

  Normalize.cpp

  Dominic Mazzoni
  Vaughan Johnson (Preview)
  Max Maisel (Loudness)

*******************************************************************//**

\class EffectNormalize
\brief An Effect to bring the peak level up to a chosen level.

*//*******************************************************************/


#include "../Audacity.h" // for rint from configwin.h
#include "Normalize.h"

#include <math.h>

#include <wx/intl.h>
#include <wx/valgen.h>

#include "../Internat.h"
#include "../Prefs.h"
#include "../ShuttleGui.h"
#include "../WaveTrack.h"
#include "../widgets/valnum.h"

// Define keys, defaults, minimums, and maximums for the effect parameters
//
//     Name         Type     Key                        Def      Min      Max   Scale
Param( PeakLevel,   double,  wxT("PeakLevel"),           -1.0,    -145.0,  0.0,  1  );
Param( LUFSLevel,   double,  wxT("LUFSLevel"),           -23.0,   -145.0,  0.0,  1  );
Param( RemoveDC,    bool,    wxT("RemoveDcOffset"),      true,    false,   true, 1  );
Param( ApplyGain,   bool,    wxT("ApplyGain"),           true,    false,   true, 1  );
Param( StereoInd,   bool,    wxT("StereoIndependent"),   false,   false,   true, 1  );
Param( UseLoudness, bool,    wxT("UseLoudness"),         false,   false,   true, 1  );

BEGIN_EVENT_TABLE(EffectNormalize, wxEvtHandler)
   EVT_CHECKBOX(wxID_ANY, EffectNormalize::OnUpdateUI)
   EVT_TEXT(wxID_ANY, EffectNormalize::OnUpdateUI)
END_EVENT_TABLE()

EffectNormalize::EffectNormalize()
{
   mPeakLevel = DEF_PeakLevel;
   mLUFSLevel = DEF_LUFSLevel;
   mDC = DEF_RemoveDC;
   mGain = DEF_ApplyGain;
   mStereoInd = DEF_StereoInd;
   mUseLoudness = DEF_UseLoudness;

   SetLinearEffectFlag(false);
}

EffectNormalize::~EffectNormalize()
{
}

// IdentInterface implementation

IdentInterfaceSymbol EffectNormalize::GetSymbol()
{
   return NORMALIZE_PLUGIN_SYMBOL;
}

wxString EffectNormalize::GetDescription()
{
   return _("Sets the peak amplitude or loudness of one or more tracks");
}

wxString EffectNormalize::ManualPage()
{
   return wxT("Normalize");
}

// EffectDefinitionInterface implementation

EffectType EffectNormalize::GetType()
{
   return EffectTypeProcess;
}

// EffectClientInterface implementation
bool EffectNormalize::DefineParams( ShuttleParams & S ){
   S.SHUTTLE_PARAM( mPeakLevel, PeakLevel );
   S.SHUTTLE_PARAM( mLUFSLevel, LUFSLevel );
   S.SHUTTLE_PARAM( mGain, ApplyGain );
   S.SHUTTLE_PARAM( mDC, RemoveDC );
   S.SHUTTLE_PARAM( mStereoInd, StereoInd );
   S.SHUTTLE_PARAM( mUseLoudness, UseLoudness );
   return true;
}

bool EffectNormalize::GetAutomationParameters(CommandParameters & parms)
{
   parms.Write(KEY_PeakLevel, mPeakLevel);
   parms.Write(KEY_LUFSLevel, mLUFSLevel);
   parms.Write(KEY_ApplyGain, mGain);
   parms.Write(KEY_RemoveDC, mDC);
   parms.Write(KEY_StereoInd, mStereoInd);
   parms.Write(KEY_UseLoudness, mUseLoudness);

   return true;
}

bool EffectNormalize::SetAutomationParameters(CommandParameters & parms)
{
   ReadAndVerifyDouble(PeakLevel);
   ReadAndVerifyDouble(LUFSLevel);
   ReadAndVerifyBool(ApplyGain);
   ReadAndVerifyBool(RemoveDC);
   ReadAndVerifyBool(StereoInd);
   ReadAndVerifyBool(UseLoudness);

   mPeakLevel = PeakLevel;
   mLUFSLevel = LUFSLevel;
   mGain = ApplyGain;
   mDC = RemoveDC;
   mStereoInd = StereoInd;
   mUseLoudness = UseLoudness;

   return true;
}

// Effect implementation

bool EffectNormalize::CheckWhetherSkipEffect()
{
   return ((mGain == false) && (mDC == false));
}

bool EffectNormalize::Startup()
{
   wxString base = wxT("/Effects/Normalize/");

   // Migrate settings from 2.1.0 or before

   // Already migrated, so bail
   if (gPrefs->Exists(base + wxT("Migrated")))
   {
      return true;
   }

   // Load the old "current" settings
   if (gPrefs->Exists(base))
   {
      int boolProxy = gPrefs->Read(base + wxT("RemoveDcOffset"), 1);
      mDC = (boolProxy == 1);
      boolProxy = gPrefs->Read(base + wxT("Normalize"), 1);
      mGain = (boolProxy == 1);
      gPrefs->Read(base + wxT("Level"), &mPeakLevel, -1.0);
      if(mPeakLevel > 0.0)  // this should never happen
         mPeakLevel = -mPeakLevel;
      boolProxy = gPrefs->Read(base + wxT("StereoIndependent"), 0L);
      mStereoInd = (boolProxy == 1);
      mUseLoudness = false;
      mLUFSLevel = DEF_LUFSLevel;

      SaveUserPreset(GetCurrentSettingsGroup());

      // Do not migrate again
      gPrefs->Write(base + wxT("Migrated"), true);
      gPrefs->Flush();
   }

   return true;
}

bool EffectNormalize::Process()
{
   if (mGain == false && mDC == false)
      return true;

   float ratio;
   if( mGain )
   {
      if(mUseLoudness)
         // LU use 10*log10(...) instead of 20*log10(...)
         // so multiply level by 2 and use standard DB_TO_LINEAR macro.
         ratio = DB_TO_LINEAR(TrapDouble(mLUFSLevel*2, MIN_LUFSLevel, MAX_LUFSLevel));
      else
         // same value used for all tracks
         ratio = DB_TO_LINEAR(TrapDouble(mPeakLevel, MIN_PeakLevel, MAX_PeakLevel));
   }
   else
      ratio = 1.0;

   //Iterate over each track
   this->CopyInputTracks(); // Set up mOutputTracks.
   bool bGoodResult = true;
   SelectedTrackListOfKindIterator iter(Track::Wave, mOutputTracks.get());
   WaveTrack *track = (WaveTrack *) iter.First();
   WaveTrack *prevTrack;
   prevTrack = track;
   double progress = 0;
   wxString topMsg;
   if(mDC && mGain)
      topMsg = _("Removing DC offset and Normalizing...\n");
   else if(mDC && !mGain)
      topMsg = _("Removing DC offset...\n");
   else if(!mDC && mGain)
      topMsg = _("Normalizing without removing DC offset...\n");
   else if(!mDC && !mGain)
      topMsg = _("Not doing anything...\n");   // shouldn't get here

   while (track) {
      //Get start and end times from track
      double trackStart = track->GetStartTime();
      double trackEnd = track->GetEndTime();

      //Set the current bounds to whichever left marker is
      //greater and whichever right marker is less:
      mCurT0 = mT0 < trackStart? trackStart: mT0;
      mCurT1 = mT1 > trackEnd? trackEnd: mT1;

      // Process only if the right marker is to the right of the left marker
      if (mCurT1 > mCurT0) {
         wxString msg;
         auto trackName = track->GetName();

         if(!track->GetLinked() || mStereoInd)
            msg =
               topMsg + wxString::Format( _("Analyzing: %s"), trackName );
         else
            msg =
               topMsg + wxString::Format( _("Analyzing first track of stereo pair: %s"), trackName );
         float offset, extent;
         bGoodResult = AnalyseTrack(track, msg, progress, offset, extent);
         if (!bGoodResult )
             break;
         if(!track->GetLinked() || mStereoInd) {
            // mono or 'stereo tracks independently'
            if( (extent > 0) && mGain )
            {
               mMult = ratio / extent;
               if(mUseLoudness)
               {
                  // LUFS is defined as -0.691 dB + 10*log10(sum(channels))
                  mMult /= 0.8529037031;
                  // LUFS are related to square values so the multiplier must be the root.
                  mMult = sqrt(ratio / extent);
               }
            }
            else
               mMult = 1.0;

            msg =
               topMsg + wxString::Format( _("Processing: %s"), trackName );
            if(track->GetLinked() || prevTrack->GetLinked())  // only get here if there is a linked track but we are processing independently
               msg =
                  topMsg + wxString::Format( _("Processing stereo channels independently: %s"), trackName );

            if (!ProcessOne(track, msg, progress, offset))
            {
               bGoodResult = false;
               break;
            }
         }
         else
         {
            // we have a linked stereo track
            // so we need to find it's min, max and offset
            // as they are needed to calc the multiplier for both tracks

            track = (WaveTrack *) iter.Next();  // get the next one
            msg =
               topMsg + wxString::Format( _("Analyzing second track of stereo pair: %s"), trackName );

            float offset2, extent2;
            bGoodResult = AnalyseTrack(track, msg, progress, offset2, extent2);
            if ( !bGoodResult )
                break;

            if (mUseLoudness)
            {
               // Loudness: use sum of both tracks.
               // As a result, stereo tracks appear about 3 LUFS louder, as specified.
               extent = extent + extent2;
               // LUFS is defined as -0.691 dB + 10*log10(sum(channels))
               extent *= 0.8529037031;
            }
            else
               // Peak: use maximum of both tracks.
               extent = fmax(extent, extent2);

            if( (extent > 0) && mGain )
            {
               mMult = ratio / extent; // we need to use this for both linked tracks
               if(mUseLoudness)
                  // LUFS are related to square values so the multiplier must be the root.
                  mMult = sqrt(mMult);
            }
            else
               mMult = 1.0;
            track = (WaveTrack *) iter.Prev();  // go back to the first linked one
            msg =
               topMsg + wxString::Format( _("Processing first track of stereo pair: %s"), trackName );
            if (!ProcessOne(track, msg, progress, offset))
            {
               bGoodResult = false;
               break;
            }
            track = (WaveTrack *) iter.Next();  // go to the second linked one
            msg =
               topMsg + wxString::Format( _("Processing second track of stereo pair: %s"), trackName );
            if (!ProcessOne(track, msg, progress, offset2))
            {
               bGoodResult = false;
               break;
            }
         }
      }

      //Iterate to the next track
      prevTrack = track;
      track = (WaveTrack *) iter.Next();
   }

   this->ReplaceProcessedTracks(bGoodResult);
   return bGoodResult;
}

void EffectNormalize::PopulateOrExchange(ShuttleGui & S)
{
   mCreating = true;

   S.StartVerticalLay(0);
   {
      S.StartMultiColumn(2, wxALIGN_CENTER);
      {
         S.StartVerticalLay(false);
         {
            mDCCheckBox = S.AddCheckBox(_("Remove DC offset (center on 0.0 vertically)"),
                                        mDC ? wxT("true") : wxT("false"));
            mDCCheckBox->SetValidator(wxGenericValidator(&mDC));

            S.StartHorizontalLay(wxALIGN_LEFT, false);
            {
               // The checkbox needs to be sized for the longer prompt, and
               // which that is will depend on translation.  So decide that here.
               // (strictly we should count pixels, not characters).
               wxString prompt1 = _("Normalize peak amplitude to");
               wxString prompt2 = _("Normalize loudness to");
               wxString longerPrompt = ((prompt1.Length() > prompt2.Length()) ? prompt1 : prompt2) + "   ";

               // Now make the checkbox.
               mGainCheckBox = S.AddCheckBox(longerPrompt,
                                             mGain ? wxT("true") : wxT("false"));
               mGainCheckBox->SetValidator(wxGenericValidator(&mGain));
               mGainCheckBox->SetMinSize( mGainCheckBox->GetSize());

               FloatingPointValidator<double> vldLevel(2, &mPeakLevel,
                                                       NumValidatorStyle::ONE_TRAILING_ZERO);
               vldLevel.SetRange( MIN_PeakLevel, MAX_PeakLevel);

               mLevelTextCtrl = S.AddTextBox( {}, wxT(""), 10);
               mLevelTextCtrl->SetName( _("Peak amplitude dB"));
               mLevelTextCtrl->SetValidator(vldLevel);
               mLeveldB = S.AddVariableText(_("dB"), false,
                                            wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
               mWarning = S.AddVariableText( {}, false,
                                            wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            }
            S.EndHorizontalLay();

            mUseLoudnessCheckBox = S.AddCheckBox(_("Use loudness instead of peak amplitude"),
                                                 mUseLoudness ? wxT("true") : wxT("false"));
            mUseLoudnessCheckBox->SetValidator(wxGenericValidator(&mGUIUseLoudness));

            mStereoIndCheckBox = S.AddCheckBox(_("Normalize stereo channels independently"),
                                               mStereoInd ? wxT("true") : wxT("false"));
            mStereoIndCheckBox->SetValidator(wxGenericValidator(&mStereoInd));
         }
         S.EndVerticalLay();
      }
      S.EndMultiColumn();
   }
   S.EndVerticalLay();
   // To ensure that the UpdateUI on creation sets the prompts correctly.
   mUseLoudness = !mGUIUseLoudness;
   mCreating = false;
}

bool EffectNormalize::TransferDataToWindow()
{
   if (!mUIParent->TransferDataToWindow())
   {
      return false;
   }

   UpdateUI();

   return true;
}

bool EffectNormalize::TransferDataFromWindow()
{
   if (!mUIParent->Validate() || !mUIParent->TransferDataFromWindow())
   {
      return false;
   }

   return true;
}

// EffectNormalize implementation

bool EffectNormalize::AnalyseTrack(const WaveTrack * track, const wxString &msg,
                                   double &progress, float &offset, float &extent)
{
   bool result = true;
   float min=0, max=0;

   if(mGain)
   {
      if(mUseLoudness)
      {
         CalcEBUR128HPF(track->GetRate());
         CalcEBUR128HSF(track->GetRate());
         if(mDC)
         {
            result = AnalyseTrackData(track, msg, progress, ANALYSE_LOUDNESS_DC, offset);
         }
         else
         {
            result = AnalyseTrackData(track, msg, progress, ANALYSE_LOUDNESS, offset);
            offset = 0.0;
         }

         // EBU R128: z_i = mean square without root
         extent = mSqSum / mCount.as_double();
      }
      else
      {
         // Since we need complete summary data, we need to block until the OD tasks are done for this track
         // This is needed for track->GetMinMax
         // TODO: should we restrict the flags to just the relevant block files (for selections)
         while (track->GetODFlags()) {
            // update the gui
            if (ProgressResult::Cancelled == mProgress->Update(
               0, _("Waiting for waveform to finish computing...")) )
               return false;
            wxMilliSleep(100);
         }

         // set mMin, mMax.  No progress bar here as it's fast.
         auto pair = track->GetMinMax(mCurT0, mCurT1); // may throw
         min = pair.first, max = pair.second;

         if(mDC)
         {
            result = AnalyseTrackData(track, msg, progress, ANALYSE_DC, offset);
            min += offset;
            max += offset;
         }
      }
   }
   else if(mDC)
   {
      min = -1.0, max = 1.0;   // sensible defaults?
      result = AnalyseTrackData(track, msg, progress, ANALYSE_DC, offset);
      min += offset;
      max += offset;
   }
   else
   {
      min = -1.0, max = 1.0;   // sensible defaults?
      offset = 0.0;
   }

   if(!mUseLoudness)
      extent = fmax(fabs(min), fabs(max));
   return result;
}

//AnalyseTrackData() takes a track, transforms it to bunch of buffer-blocks,
//and executes selected AnalyseOperation on it...
bool EffectNormalize::AnalyseTrackData(const WaveTrack * track, const wxString &msg,
                                double &progress, AnalyseOperation op, float &offset)
{
   bool rc = true;

   //Transform the marker timepoints to samples
   auto start = track->TimeToLongSamples(mCurT0);
   auto end = track->TimeToLongSamples(mCurT1);

   //Get the length of the buffer (as double). len is
   //used simply to calculate a progress meter, so it is easier
   //to make it a double now than it is to do it later
   auto len = (end - start).as_double();

   //Initiate a processing buffer.  This buffer will (most likely)
   //be shorter than the length of the track being processed.
   Floats buffer{ track->GetMaxBlockSize() };

   mSum   = 0.0; // dc offset inits
   mSqSum = 0.0; // rms init
   mCount = 0;

   sampleCount blockSamples;
   sampleCount totalSamples = 0;

   //Go through the track one buffer at a time. s counts which
   //sample the current buffer starts at.
   auto s = start;
   while (s < end) {
      //Get a block of samples (smaller than the size of the buffer)
      //Adjust the block size if it is the final block in the track
      const auto block = limitSampleBufferSize(
         track->GetBestBlockSize(s),
         end - s
      );

      //Get the samples from the track and put them in the buffer
      track->Get((samplePtr) buffer.get(), floatSample, s, block, fillZero, true, &blockSamples);
      totalSamples += blockSamples;

      //Process the buffer.
      if(op == ANALYSE_DC)
         AnalyseDataDC(buffer.get(), block);
      else if(op == ANALYSE_LOUDNESS)
         AnalyseDataLoudness(buffer.get(), block);
      else if(op == ANALYSE_LOUDNESS_DC)
         AnalyseDataLoudnessDC(buffer.get(), block);

      //Increment s one blockfull of samples
      s += block;

      //Update the Progress meter
      if (TotalProgress(progress +
                        ((s - start).as_double() / len)/double(2*GetNumWaveTracks()), msg)) {
         rc = false; //lda .. break, not return, so that buffer is deleted
         break;
      }
   }
   if( totalSamples > 0 )
      offset = -mSum / totalSamples.as_double();  // calculate actual offset (amount that needs to be added on)
   else
      offset = 0.0;

   progress += 1.0/double(2*GetNumWaveTracks());
   //Return true because the effect processing succeeded ... unless cancelled
   return rc;
}

//ProcessOne() takes a track, transforms it to bunch of buffer-blocks,
//and executes ProcessData, on it...
// uses mMult and offset to normalize a track.
// mMult must be set before this is called
bool EffectNormalize::ProcessOne(
   WaveTrack * track, const wxString &msg, double &progress, float offset)
{
   bool rc = true;

   //Transform the marker timepoints to samples
   auto start = track->TimeToLongSamples(mCurT0);
   auto end = track->TimeToLongSamples(mCurT1);

   //Get the length of the buffer (as double). len is
   //used simply to calculate a progress meter, so it is easier
   //to make it a double now than it is to do it later
   auto len = (end - start).as_double();

   //Initiate a processing buffer.  This buffer will (most likely)
   //be shorter than the length of the track being processed.
   Floats buffer{ track->GetMaxBlockSize() };

   //Go through the track one buffer at a time. s counts which
   //sample the current buffer starts at.
   auto s = start;
   while (s < end) {
      //Get a block of samples (smaller than the size of the buffer)
      //Adjust the block size if it is the final block in the track
      const auto block = limitSampleBufferSize(
         track->GetBestBlockSize(s),
         end - s
      );

      //Get the samples from the track and put them in the buffer
      track->Get((samplePtr) buffer.get(), floatSample, s, block);

      //Process the buffer.
      ProcessData(buffer.get(), block, offset);

      //Copy the newly-changed samples back onto the track.
      track->Set((samplePtr) buffer.get(), floatSample, s, block);

      //Increment s one blockfull of samples
      s += block;

      //Update the Progress meter
      if (TotalProgress(progress +
                        ((s - start).as_double() / len)/double(2*GetNumWaveTracks()), msg)) {
         rc = false; //lda .. break, not return, so that buffer is deleted
         break;
      }
   }
   progress += 1.0/double(2*GetNumWaveTracks());

   //Return true because the effect processing succeeded ... unless cancelled
   return rc;
}

/// @see AnalyseDataLoudnessDC
void EffectNormalize::AnalyseDataDC(float *buffer, size_t len)
{
   for(decltype(len) i = 0; i < len; i++)
      mSum += (double)buffer[i];
   mCount += len;
}

/// @see AnalyseDataLoudnessDC
void EffectNormalize::AnalyseDataLoudness(float *buffer, size_t len)
{
   float value;
   for(decltype(len) i = 0; i < len; i++)
   {
      value = mR128HSF.ProcessOne(buffer[i]);
      value = mR128HPF.ProcessOne(value);
      mSqSum += ((double)value) * ((double)value);
   }
   mCount += len;
}

/// Calculates sample sum (for DC) and EBU R128 weighted square sum
/// (for loudness). This function has variants which only calculate
/// sum or square sum for performance improvements if only one of those
/// values is required.
/// @see AnalyseDataLoudness
/// @see AnalyseDataDC
void EffectNormalize::AnalyseDataLoudnessDC(float *buffer, size_t len)
{
   float value;
   for(decltype(len) i = 0; i < len; i++)
   {
      mSum += (double)buffer[i];
      value = mR128HSF.ProcessOne(buffer[i]);
      value = mR128HPF.ProcessOne(value);
      mSqSum += ((double)value) * ((double)value);
   }
   mCount += len;
}

void EffectNormalize::ProcessData(float *buffer, size_t len, float offset)
{
   for(decltype(len) i = 0; i < len; i++) {
      float adjFrame = (buffer[i] + offset) * mMult;
      buffer[i] = adjFrame;
   }
}

// EBU R128 parameter sampling rate adaption after
// Mansbridge, Stuart, Saoirse Finn, and Joshua D. Reiss.
// "Implementation and Evaluation of Autonomous Multi-track Fader Control."
// Paper presented at the 132nd Audio Engineering Society Convention,
// Budapest, Hungary, 2012."
void EffectNormalize::CalcEBUR128HPF(float fs)
{
   double f0 = 38.13547087602444;
   double Q  =  0.5003270373238773;
   double K  = tan(M_PI * f0 / fs);

   mR128HPF.Reset();

   mR128HPF.fNumerCoeffs[Biquad::B0] =  1.0;
   mR128HPF.fNumerCoeffs[Biquad::B1] = -2.0;
   mR128HPF.fNumerCoeffs[Biquad::B2] =  1.0;

   mR128HPF.fDenomCoeffs[Biquad::A1] = 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K);
   mR128HPF.fDenomCoeffs[Biquad::A2] = (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K);
}

// EBU R128 parameter sampling rate adaption after
// Mansbridge, Stuart, Saoirse Finn, and Joshua D. Reiss.
// "Implementation and Evaluation of Autonomous Multi-track Fader Control."
// Paper presented at the 132nd Audio Engineering Society Convention,
// Budapest, Hungary, 2012."
void EffectNormalize::CalcEBUR128HSF(float fs)
{
   double db =    3.999843853973347;
   double f0 = 1681.974450955533;
   double Q  =    0.7071752369554196;
   double K  = tan(M_PI * f0 / fs);

   double Vh = pow(10.0, db / 20.0);
   double Vb = pow(Vh, 0.4996667741545416);

   double a0 = 1.0 + K / Q + K * K;

   mR128HSF.Reset();

   mR128HSF.fNumerCoeffs[Biquad::B0] = (Vh + Vb * K / Q + K * K) / a0;
   mR128HSF.fNumerCoeffs[Biquad::B1] =       2.0 * (K * K -  Vh) / a0;
   mR128HSF.fNumerCoeffs[Biquad::B2] = (Vh - Vb * K / Q + K * K) / a0;

   mR128HSF.fDenomCoeffs[Biquad::A1] =   2.0 * (K * K - 1.0) / a0;
   mR128HSF.fDenomCoeffs[Biquad::A2] = (1.0 - K / Q + K * K) / a0;
}

void EffectNormalize::OnUpdateUI(wxCommandEvent & WXUNUSED(evt))
{
   UpdateUI();
}

void EffectNormalize::UpdateUI()
{

   if (!mUIParent->TransferDataFromWindow())
   {
      mWarning->SetLabel(_(".  Maximum 0dB."));
      EnableApply(false);
      return;
   }
   mWarning->SetLabel(wxT(""));

   // Changing the prompts causes an unwanted UpdateUI event.  
   // This 'guard' stops that becoming an infinite recursion.
   if (mUseLoudness != mGUIUseLoudness)
   {
      mUseLoudness = mGUIUseLoudness;
      if (mUseLoudness)
      {
         FloatingPointValidator<double> vldLevel(2, &mLUFSLevel, NumValidatorStyle::ONE_TRAILING_ZERO);
         vldLevel.SetRange(MIN_LUFSLevel, MAX_LUFSLevel);
         mLevelTextCtrl->SetValidator(vldLevel);
         mLevelTextCtrl->SetName(_("Loudness LUFS"));
         mLevelTextCtrl->SetValue(wxString::FromDouble(mLUFSLevel));
         mLeveldB->SetLabel(_("LUFS"));
         mGainCheckBox->SetLabelText(_("Normalize loudness to"));
      }
      else
      {
         FloatingPointValidator<double> vldLevel(2, &mPeakLevel, NumValidatorStyle::ONE_TRAILING_ZERO);
         vldLevel.SetRange(MIN_PeakLevel, MAX_PeakLevel);
         mLevelTextCtrl->SetValidator(vldLevel);
         mLevelTextCtrl->SetName(_("Peak amplitude dB"));
         mLevelTextCtrl->SetValue(wxString::FromDouble(mPeakLevel));
         mLeveldB->SetLabel(_("dB"));
         mGainCheckBox->SetLabelText(_("Normalize peak amplitude to"));
      }
   }

   // Disallow level stuff if not normalizing
   mLevelTextCtrl->Enable(mGain);
   mLeveldB->Enable(mGain);
   mStereoIndCheckBox->Enable(mGain);
   mUseLoudnessCheckBox->Enable(mGain);

   // Disallow OK/Preview if doing nothing
   EnableApply(mGain || mDC);
}
