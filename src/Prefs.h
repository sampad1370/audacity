/**********************************************************************

  Audacity: A Digital Audio Editor

  Prefs.h

  Dominic Mazzoni
  Markus Meyer

  Audacity uses wxWidgets' wxFileConfig class to handle preferences.
  In Audacity versions prior to 1.3.1, it used wxConfig, which would
  store the prefs in a platform-dependent way (e.g. in the registry
  on Windows). Now it always stores the settings in a configuration file
  in the Audacity Data Directory.

  Every time we read a preference, we need to specify the default
  value for that preference, to be used if the preference hasn't
  been set before.

  So, to avoid code duplication, we provide functions in this file
  to read and write preferences which have a nonobvious default
  value, so that if we later want to change this value, we only
  have to change it in one place.

  See Prefs.cpp for a (complete?) list of preferences we keep
  track of...

**********************************************************************/
#ifndef __AUDACITY_PREFS__
#define __AUDACITY_PREFS__

#include "Audacity.h"
#include "../include/audacity/IdentInterface.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/convauto.h>

#include <iterator>
#include <vector>

void InitPreferences();
void FinishPreferences();

class AudacityPrefs;


extern AUDACITY_DLL_API AudacityPrefs *gPrefs;
extern int gMenusDirty;

class  AUDACITY_DLL_API AudacityPrefs : public wxFileConfig 
{
public:
   AudacityPrefs(const wxString& appName = wxEmptyString,
               const wxString& vendorName = wxEmptyString,
               const wxString& localFilename = wxEmptyString,
               const wxString& globalFilename = wxEmptyString,
               long style = wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_GLOBAL_FILE,
               const wxMBConv& conv = wxConvAuto());
   bool GetEditClipsCanMove();
};

// Packages a table of user-visible choices each with an internal code string,
// a preference key path,
// and a default choice
class EnumSetting
{
public:
   EnumSetting(
      const wxString &key,
      const IdentInterfaceSymbol symbols[], size_t nSymbols,
      size_t defaultSymbol
   )
      : mKey{ key }

      , mSymbols{ symbols }
      , mnSymbols{ nSymbols }

      , mDefaultSymbol{ defaultSymbol }
   {
      wxASSERT( defaultSymbol < nSymbols );
   }

   const wxString &Key() const { return mKey; }
   const IdentInterfaceSymbol &Default() const
      { return mSymbols[mDefaultSymbol]; }
   const IdentInterfaceSymbol *begin() const { return mSymbols; }
   const IdentInterfaceSymbol *end() const { return mSymbols + mnSymbols; }

   wxString Read() const;
   bool Write( const wxString &value ); // you flush gPrefs afterward

protected:
   size_t Find( const wxString &value ) const;
   virtual void Migrate( wxString& );

   const wxString mKey;

   const IdentInterfaceSymbol *mSymbols;
   const size_t mnSymbols;

   // stores an internal value
   mutable bool mMigrated { false };

   const size_t mDefaultSymbol;
};

// Extends EnumSetting with a corresponding table of integer codes
// (generally not equal to their table positions),
// and optionally an old preference key path that stored integer codes, to be
// migrated into one that stores internal string values instead
class EncodedEnumSetting : public EnumSetting
{
public:
   EncodedEnumSetting(
      const wxString &key,
      const IdentInterfaceSymbol symbols[], size_t nSymbols,
      size_t defaultSymbol,

      const int intValues[] = nullptr, // must have same size as symbols
      const wxString &oldKey = wxString("")
   )
      : EnumSetting{ key, symbols, nSymbols, defaultSymbol }
      , mIntValues{ intValues }
      , mOldKey{ oldKey }
   {
      wxASSERT( mIntValues );
   }

   // Read and write the encoded values
   virtual int ReadInt() const;
   bool WriteInt( int code ); // you flush gPrefs afterward

protected:
   size_t FindInt( int code ) const;
   void Migrate( wxString& ) override;

private:
   const int *mIntValues;
   const wxString mOldKey;
};

struct PrefsDefaultConfigRef { static wxConfigBase* get() { return wxConfigBase::Get(); } };
struct PrefsGlobalConfigRef { static wxConfigBase* get() { return gPrefs; } };

template<typename TValue, typename TConfigRef = PrefsGlobalConfigRef>
class PreferenceVariable final
{
   const wxString name_;
   const TValue defaultValue_;

   wxConfigBase* config() const { return TConfigRef::get(); }
public:
   explicit PreferenceVariable(const wxString& name, TValue defaultValue = TValue())
      : name_(name), defaultValue_(defaultValue)
   { }
   explicit PreferenceVariable(wxString&& name, TValue defaultValue = TValue())
      : name_(std::move(name)), defaultValue_(defaultValue)
   { }

   const wxString& name() const { return name_; }

   bool Exists() const { return config()->HasEntry(name_); }
   bool Delete(bool groupIfEmptyAlso = true) const { return config()->DeleteEntry(name_, groupIfEmptyAlso); }

   bool Read(TValue& value) const { return config()->Read(name_, &value, defaultValue_); }
   TValue Read() const { return config()->Read(name_, defaultValue_); }

   bool Write(const TValue& value) const { return config()->Write(name_, value); }
   bool Write(TValue&& value) const { return config()->Write(name_, value); }
};

template<>
inline bool PreferenceVariable<bool>::Read() const { return gPrefs->ReadBool(name_, defaultValue_); }
template<>
inline long PreferenceVariable<long>::Read() const { return gPrefs->ReadLong(name_, defaultValue_); }
template<>
inline double PreferenceVariable<double>::Read() const { return gPrefs->ReadDouble(name_, defaultValue_); }

typedef PreferenceVariable<wxString> PreferenceVariableString;
typedef PreferenceVariable<bool> PreferenceVariableBool;
typedef PreferenceVariable<long> PreferenceVariableLong;
typedef PreferenceVariable<double> PreferenceVariableDouble;

template<typename TConfigRef = PrefsGlobalConfigRef>
class PreferenceGroup final
{
   const wxString name_;

   wxConfigBase* config() const { return TConfigRef::get(); }
public:
   explicit PreferenceGroup(const wxString& name) : name_(name)
   { }
   explicit PreferenceGroup(wxString&& name)
      : name_(std::move(name))
   { }

   const wxString& name() const { return name_; }

   bool Exists() const { return config()->HasGroup(name_); }
   bool Delete() const { return config()->DeleteGroup(name_); }

   std::vector<wxString> Groups() const;
   std::vector<wxString> Entries() const;
};


template<typename TConfigRef>
std::vector<wxString> PreferenceGroup<TConfigRef>::Groups() const
{
   auto oldPath = gPrefs->GetPath();
   gPrefs->SetPath(name_);

   std::vector<wxString> v;

   long index;
   wxString name;

   auto group = config()->GetFirstGroup(name, index);

   while (group)
   {
      v.push_back(name);

      group = config()->GetNextGroup(name, index);
   }

   config()->SetPath(oldPath);

   return v;
}

template<typename TConfigRef>
std::vector<wxString> PreferenceGroup<TConfigRef>::Entries() const
{
   auto oldPath = gPrefs->GetPath();
   config()->SetPath(name_);

   std::vector<wxString> v;

   long index;
   wxString name;

   auto entry = config()->GetFirstEntry(name, index);

   while (entry)
   {
      v.push_back(name);

      entry = config()->GetNextEntry(name, index);
   }

   config()->SetPath(oldPath);

   return v;
}

#endif
