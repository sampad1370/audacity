/**********************************************************************

  Audacity: A Digital Audio Editor

  BatchPrefs.h

  Dominic Mazzoni
  James Crook

**********************************************************************/

#ifndef __AUDACITY_BATCH_PREFS__
#define __AUDACITY_BATCH_PREFS__

#include <wx/defs.h>

#include <wx/window.h>

#include "PrefsPanel.h"

class ShuttleGui;

class BatchPrefs final : public PrefsPanel
{
public:
   BatchPrefs(wxWindow * parent, wxWindowID winid);
   ~BatchPrefs();
   bool Commit() override;
   void PopulateOrExchange(ShuttleGui & S) override;

private:
   void Populate();

   DECLARE_EVENT_TABLE()
};

class BatchPrefsFactory final : public PrefsPanelFactory
{
public:
   PrefsPanel *operator () (wxWindow *parent, wxWindowID winid) const override;
};
#endif
