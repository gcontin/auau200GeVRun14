#include <vector>
#include <cmath>

#include "TTree.h"
#include "TFile.h"
#include "TString.h"
#include "StThreeVectorF.hh"
#include "StLorentzVectorF.hh"
#include "../StPicoDstMaker/StPicoDst.h"
#include "../StPicoDstMaker/StPicoDstMaker.h"
#include "../StPicoDstMaker/StPicoEvent.h"
#include "../StPicoDstMaker/StPicoTrack.h"
#include "../StPicoDstMaker/StPicoBTofPidTraits.h"
#include "StPicoD0Event.h"
#include "StPicoD0Hists.h"
#include "StKaonPion.h"
#include "StCuts.h"

#include "StPicoD0EventMaker.h"

ClassImp(StPicoD0EventMaker)

StPicoD0EventMaker::StPicoD0EventMaker(char const* makerName, StPicoDstMaker* picoMaker, char const* fileBaseName)
   : StMaker(makerName), mPicoDstMaker(picoMaker), mPicoEvent(NULL), mPicoD0Hists(NULL), 
     mKfVertexFitter(), mOutputFile(NULL), mTree(NULL), mPicoD0Event(NULL) 
{
   mPicoD0Event = new StPicoD0Event();

   TString baseName(fileBaseName);
   mOutputFile = new TFile(Form("%s.picoD0.root",fileBaseName), "RECREATE");
   mOutputFile->SetCompressionLevel(1);
   int BufSize = (int)pow(2., 16.);
   int Split = 1;
   mTree = new TTree("T", "T", BufSize);
   mTree->SetAutoSave(1000000); // autosave every 1 Mbytes
   mTree->Branch("dEvent", "StPicoD0Event", &mPicoD0Event, BufSize, Split);

   mPicoD0Hists = new StPicoD0Hists(fileBaseName);
}

StPicoD0EventMaker::~StPicoD0EventMaker()
{
   /* mTree is owned by mOutputFile directory, it will be destructed once
    * the file is closed in ::Finish() */
   delete mPicoD0Hists;
}

Int_t StPicoD0EventMaker::Init()
{
   return kStOK;
}

Int_t StPicoD0EventMaker::Finish()
{
   mOutputFile->cd();
   mOutputFile->Write();
   mOutputFile->Close();
   mPicoD0Hists->closeFile();
   return kStOK;
}

void StPicoD0EventMaker::Clear(Option_t *opt)
{
   mPicoD0Event->clear("C");
}

Int_t StPicoD0EventMaker::Make()
{
   if (!mPicoDstMaker)
   {
      LOG_WARN << " No PicoDstMaker! Skip! " << endm;
      return kStWarn;
   }

   StPicoDst const * picoDst = mPicoDstMaker->picoDst();
   if (!picoDst)
   {
      LOG_WARN << " No PicoDst! Skip! " << endm;
      return kStWarn;
   }

   mPicoEvent = picoDst->event();
   mPicoD0Event->addPicoEvent(*mPicoEvent);

   if (isGoodEvent())
   {
      UInt_t nTracks = picoDst->numberOfTracks();

      std::vector<unsigned short> idxPicoKaons;
      std::vector<unsigned short> idxPicoPions;

      unsigned int nHftTracks = 0;

      for (unsigned short iTrack = 0; iTrack < nTracks; ++iTrack)
      {
         StPicoTrack* trk = picoDst->track(iTrack);

         if (!trk || !isGoodTrack(trk)) continue;
         ++nHftTracks;

         if (isPion(trk)) idxPicoPions.push_back(iTrack);
         if (isKaon(trk)) idxPicoKaons.push_back(iTrack);

      } // .. end tracks loop

      float const bField = mPicoEvent->bField();
      StThreeVectorF const pVtx = mPicoEvent->primaryVertex();

      mPicoD0Event->nKaons(idxPicoKaons.size());
      mPicoD0Event->nPions(idxPicoPions.size());

      for (unsigned short ik = 0; ik < idxPicoKaons.size(); ++ik)
      {
         StPicoTrack const * kaon = picoDst->track(idxPicoKaons[ik]);

         // make Kπ pairs
         for (unsigned short ip = 0; ip < idxPicoPions.size(); ++ip)
         {
            if (idxPicoKaons[ik] == idxPicoPions[ip]) continue;

            StPicoTrack const * pion = picoDst->track(idxPicoPions[ip]);

            StKaonPion kaonPion(kaon, pion, idxPicoKaons[ik], idxPicoPions[ip], pVtx, bField);


            if (!isGoodPair(kaonPion)) continue;

            mPicoD0Event->addKaonPion(&kaonPion);

            if(kaon->charge() * pion->charge() <0) // fill histograms for unlike sign pairs only
            {
              bool fillMass = isGoodQaPair(&kaonPion,*kaon,*pion);
              mPicoD0Hists->addKaonPion(&kaonPion,fillMass);
            }

         } // .. end make Kπ pairs
      } // .. end of kaons loop

      mPicoD0Hists->addEvent(*mPicoEvent,*mPicoD0Event,nHftTracks);
      idxPicoKaons.clear();
      idxPicoPions.clear();
   } //.. end of good event fill

   // This should never be inside the good event block
   // because we want to save header information about all events, good or bad
   mTree->Fill();
   mPicoD0Event->clear("C");

   return kStOK;
}

bool StPicoD0EventMaker::isGoodEvent()
{
   return (mPicoEvent->triggerWord() & cuts::triggerWord) &&
          fabs(mPicoEvent->primaryVertex().z()) < cuts::vz &&
          fabs(mPicoEvent->primaryVertex().z() - mPicoEvent->vzVpd()) < cuts::vzVpdVz;
}
bool StPicoD0EventMaker::isGoodTrack(StPicoTrack const * const trk) const
{
   // Require at least one hit on every layer of PXL and IST.
   // It is done here for tests on the preview II data.
   // The new StPicoTrack which is used in official production has a method to check this
   return (!cuts::requireHFT || trk->isHFTTrack()) && 
          trk->nHitsFit() >= cuts::nHitsFit;
}
bool StPicoD0EventMaker::isPion(StPicoTrack const * const trk) const
{
   return fabs(trk->nSigmaPion()) < cuts::nSigmaPion;
}
bool StPicoD0EventMaker::isKaon(StPicoTrack const * const trk) const
{
   return fabs(trk->nSigmaKaon()) < cuts::nSigmaKaon;
}
bool StPicoD0EventMaker::isGoodPair(StKaonPion const & kp) const
{
   return kp.m() > cuts::minMass && kp.m() < cuts::maxMass &&
          std::cos(kp.pointingAngle()) > cuts::cosTheta &&
          kp.decayLength() > cuts::decayLength &&
          kp.dcaDaughters() < cuts::dcaDaughters;
}
bool  StPicoD0EventMaker::isGoodQaPair(StKaonPion const& kp, StPicoTrack const& kaon,StPicoTrack const& pion)
{
  return pion.gPt() >= cuts::qaPt && kaon.gPt() >= cuts::qaPt && 
         pion.nHitsFit() >= cuts::qaNHitsFit && kaon.nHitsFit() >= cuts::qaNHitsFit &&
         fabs(kaon.nSigmaKaon()) < cuts::qaNSigmaKaon && 
         cos(kp.pointingAngle()) > cuts::qaCosTheta &&
         kp.pionDca() > cuts::qaPDca && kp.kaonDca() > cuts::qaKDca &&
         kp.dcaDaughters() < cuts::qaDcaDaughters;
}
