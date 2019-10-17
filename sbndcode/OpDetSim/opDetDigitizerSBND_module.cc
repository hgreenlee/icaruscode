////////////////////////////////////////////////////////////////////////
// Class:       opDetDigitizerSBND
// Module Type: producer
// File:        opDetDigitizerSBND_module.cc
//
// Generated at Fri Apr  5 09:21:15 2019 by Laura Paulucci Marinho using artmod
// from cetpkgsupport v1_14_01.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "canvas/Utilities/InputTag.h"
#include "canvas/Utilities/Exception.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/TableFragment.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>
#include <vector>
#include <cmath>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <sstream>
#include <fstream>

#include "lardataobj/RawData/OpDetWaveform.h"
#include "lardata/DetectorInfoServices/DetectorClocksServiceStandard.h"
#include "larcore/Geometry/Geometry.h"
#include "lardataobj/Simulation/sim.h"
#include "lardataobj/Simulation/SimChannel.h"
#include "lardataobj/Simulation/SimPhotons.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"

#include "TMath.h"
#include "TH1D.h"
#include "TRandom3.h"
#include "TF1.h"

#include "sbndPDMapAlg.h" 
#include "DigiArapucaSBNDAlg.h" 
#include "DigiPMTSBNDAlg.h" 
#include "opDetSBNDTriggerAlg.h"

namespace opdet{

 /*
 * This module simulates the digitization of SBND photon detectors response.
 * 
 * The module is has an interface to the simulation algorithms for PMTs and arapucas,
 * opdet::DigiPMTSBNDAlg e opdet::DigiArapucaSBNDAlg.
 * 
 * Input
 * ======
 * The module utilizes as input a collection of `sim::SimPhotons` or `sim::SimPhotonsLite`, each
 * containing the photons propagated to a single optical detector channel.
 * 
 * Output
 * =======
 * A collection of optical detector waveforms (`std::vector<raw::OpDetWaveform>`) is produced.
 * 
 * Requirements
 * =============
 * This module currently requires LArSoft services:
 * * `DetectorClocksService` for timing conversions and settings
 * * `LArPropertiesService` for the scintillation yield(s)
 * 
 */

  class opDetDigitizerSBND;

  class opDetDigitizerSBND : public art::EDProducer {
  public:
   struct Config
    {
        using Comment = fhicl::Comment;
        using Name = fhicl::Name;
        
        fhicl::Atom<art::InputTag> InputModuleName {
            Name("InputModule"),
            Comment("Simulated photons to be digitized")
        };
        fhicl::Atom<double> WaveformSize {
            Name("WaveformSize"),
            Comment("Value to initialize the waveform vector in ns. It is resized in the algorithms according to readout window of PDs")
        };
        fhicl::Atom<int> UseLitePhotons {
            Name("UseLitePhotons"),
            Comment("Whether SimPhotonsLite or SimPhotons will be used")
        };

        fhicl::Atom<bool> ApplyTriggers {
            Name("ApplyTriggers"),
            Comment("Whether to apply trigger algorithm to waveforms"),
            true
        };
        
        fhicl::TableFragment<opdet::DigiPMTSBNDAlgMaker::Config> pmtAlgoConfig;
        fhicl::TableFragment<opdet::DigiArapucaSBNDAlgMaker::Config> araAlgoConfig;
        fhicl::TableFragment<opdet::opDetSBNDTriggerAlg::Config> trigAlgoConfig;
    }; // struct Config

    using Parameters = art::EDProducer::Table<Config>;

    explicit opDetDigitizerSBND(Parameters const& config);
    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.

    // Plugins should not be copied or assigned.
    opDetDigitizerSBND(opDetDigitizerSBND const &) = delete;
    opDetDigitizerSBND(opDetDigitizerSBND &&) = delete;
    opDetDigitizerSBND & operator = (opDetDigitizerSBND const &) = delete;
    opDetDigitizerSBND & operator = (opDetDigitizerSBND &&) = delete;

    // Required functions.
    void produce(art::Event & e) override;

    opdet::sbndPDMapAlg map; //map for photon detector types
    unsigned int nChannels = map.size();
    unsigned int fNsamples; //Samples per waveform
    std::vector<raw::OpDetWaveform> fWaveforms; // holder for un-triggered waveforms

  private:

  // Declare member data here.
    art::InputTag fInputModuleName;

    double fSampling;       //wave sampling frequency (GHz)
    double fWaveformSize;  //waveform time interval (ns)

    int fUseLitePhotons; //1 for using SimLitePhotons and 0 for SimPhotons (more complete)
    bool fApplyTriggers;
    std::unordered_map< raw::Channel_t,std::vector<double> > fFullWaveforms;  


    void CreateDirectPhotonMap(std::map<int,sim::SimPhotons>& auxmap, std::vector< art::Handle< std::vector< sim::SimPhotons > > > photon_handles);
    void CreateDirectPhotonMapLite(std::map<int,sim::SimPhotonsLite>& auxmap, std::vector< art::Handle< std::vector< sim::SimPhotonsLite > > > photon_handles);

    void MakeWaveforms(const art::Event &e, opdet::DigiPMTSBNDAlg *pmtDigitizer, opdet::DigiArapucaSBNDAlg *arapucaDigitizer);

//arapuca and PMT digitization algorithms
    opdet::DigiPMTSBNDAlgMaker makePMTDigi;
    opdet::DigiArapucaSBNDAlgMaker makeArapucaDigi;

    // trigger algorithm
    opdet::opDetSBNDTriggerAlg fTriggerAlg;
  };

  opDetDigitizerSBND::opDetDigitizerSBND(Parameters const& config)
  : EDProducer{config}
  , fInputModuleName(config().InputModuleName())
  , fWaveformSize(config().WaveformSize())
  , fUseLitePhotons(config().UseLitePhotons())
  , fApplyTriggers(config().ApplyTriggers())
  , makePMTDigi(config().pmtAlgoConfig())
  , makeArapucaDigi(config().araAlgoConfig())
  , fTriggerAlg(config().trigAlgoConfig(), lar::providerFrom<detinfo::DetectorClocksService>(), lar::providerFrom<detinfo::DetectorPropertiesService>())
  {
  // Call appropriate produces<>() functions here.
    produces< std::vector< raw::OpDetWaveform > >();

    auto const *timeService = lar::providerFrom< detinfo::DetectorClocksService >();
    fSampling = (timeService->OpticalClock().Frequency())/1000.0; //in GHz
  
    std::cout << "Sampling = " << fSampling << " GHz." << std::endl;
  
  }

  void opDetDigitizerSBND::produce(art::Event & e)
  {
    std::unique_ptr< std::vector< raw::OpDetWaveform > > pulseVecPtr(std::make_unique< std::vector< raw::OpDetWaveform > > ());
  // Implementation of required member function here.
    std::cout <<"Event: " << e.id().event() << std::endl;

    // setup the waveforms
    fWaveforms = std::vector<raw::OpDetWaveform> (nChannels);

    // prepare the algorithm
    //     
    auto arapucaDigitizer = makeArapucaDigi(
    *(lar::providerFrom<detinfo::LArPropertiesService>()),
    *(lar::providerFrom<detinfo::DetectorClocksService>())
    );

    auto pmtDigitizer = makePMTDigi(
    *(lar::providerFrom<detinfo::LArPropertiesService>()),
    *(lar::providerFrom<detinfo::DetectorClocksService>())
    );

    // Run the digitizer over the full readout window
    MakeWaveforms(e, pmtDigitizer.get(), arapucaDigitizer.get());

    
    if (fApplyTriggers) {
      // find the trigger locations for the waveforms
      for (const raw::OpDetWaveform &waveform: fWaveforms) {
        raw::Channel_t ch = waveform.ChannelNumber();
        // skip light channels which don't correspond to readout channels
        if (ch == std::numeric_limits<raw::Channel_t>::max() /* "NULL" value*/) {
          continue;
        }
        raw::ADC_Count_t baseline = (map.pdType(ch, "barepmt") || map.pdType(ch, "pmt")) ? 
          pmtDigitizer->Baseline() : arapucaDigitizer->Baseline();
        fTriggerAlg.FindTriggerLocations(waveform, baseline);
      }

      // combine the triggers
      fTriggerAlg.MergeTriggerLocations();

      // apply the triggers and save the output
      for (const raw::OpDetWaveform &waveform: fWaveforms) {
        if (waveform.ChannelNumber() == std::numeric_limits<raw::Channel_t>::max() /* "NULL" value*/) {
          continue;
        }
        std::vector<raw::OpDetWaveform> waveforms = fTriggerAlg.ApplyTriggerLocations(waveform);
        // move these waveforms into the pulseVecPtr
        pulseVecPtr->reserve(pulseVecPtr->size() + waveforms.size());
        std::move(waveforms.begin(), waveforms.end(), std::back_inserter(*pulseVecPtr));
      }
      // put the waveforms in the event
      e.put(std::move(pulseVecPtr));
      // clear out the triggers
      fTriggerAlg.ClearTriggerLocations();
    }
    else {
      // put the full waveforms in the event
      for (const raw::OpDetWaveform &waveform: fWaveforms) {
        if (waveform.ChannelNumber() == std::numeric_limits<raw::Channel_t>::max() /* "NULL" value*/) {
          continue;
        }
        pulseVecPtr->push_back(waveform);
      }
      e.put(std::move(pulseVecPtr));
    }

    // clear out the full waveforms
    fWaveforms.clear();

  }//produce end

  void opDetDigitizerSBND::MakeWaveforms(const art::Event &e, opdet::DigiPMTSBNDAlg *pmtDigitizer, opdet::DigiArapucaSBNDAlg *arapucaDigitizer)
  {
    std::array<double, 2> enable_window = fTriggerAlg.TriggerEnableWindow(); // us
    double start_time = enable_window[0];
    unsigned n_samples = ( enable_window[1] - enable_window[0]) * 1000. /*us -> ns*/ * fSampling /* GHz */;
    int ch, channel;
    if(fUseLitePhotons==1){//using SimPhotonsLite

      std::map<int,sim::SimPhotonsLite> auxmap;   // to temporarily store channel and combine PMT (direct and converted) time profiles
 
     //Get *ALL* SimPhotonsCollectionLite from Event
      std::vector< art::Handle< std::vector< sim::SimPhotonsLite > > > photon_handles;
      e.getManyByType(photon_handles);
      if (photon_handles.size() == 0)
        throw art::Exception(art::errors::ProductNotFound)<<"sim SimPhotonsLite retrieved and you requested them.";
      
      CreateDirectPhotonMapLite(auxmap, photon_handles);

      for (auto opdetHandle: photon_handles) {
 
      //this now tells you if light collection is reflected
        bool Reflected = (opdetHandle.provenance()->productInstanceName() == "Reflected");
      
        std::cout << "Number of photon channels: " << opdetHandle->size() << std::endl;

        for (auto const& litesimphotons : (*opdetHandle)){
          std::vector<short unsigned int> waveform;
	  ch = litesimphotons.OpChannel;
	  if((Reflected) && (map.pdType(ch, "barepmt") || map.pdType(ch, "pmt") )){ //All PMT channels
	//    std::cout << ch << " : PMT channel " <<std::endl;
	    pmtDigitizer->ConstructWaveformLite(ch, litesimphotons, waveform, map.pdName(ch), auxmap, start_time*1000 /*ns for digitizer*/, n_samples);
	    fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
	  }
/*	  if(map.pdType(ch, "bar")) //Paddles
	    std::cout << ch << " : Digitization not implemented for paddles. " <<std::endl;*/
	  else if((map.pdType(ch, "arapucaT1") && !Reflected) || (map.pdType(ch, "arapucaT2") && Reflected) ){//getting only arapuca channels with appropriate type of light
//	    std::cout << "Arapuca channels " <<std::endl;
	    arapucaDigitizer->ConstructWaveformLite(ch, litesimphotons, waveform, map.pdName(ch),start_time*1000 /*ns for digitizer*/, n_samples);
            fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
          }
	  else if((map.pdType(ch,"xarapucaprime") && !Reflected)){//getting only xarapuca channels with appropriate type of light (this separation is needed because xarapucas are set as two different optical channels but are actually only one readout channel)
	//    std::cout << "X-Arapuca channels " <<std::endl;
            sim::SimPhotonsLite auxLite;
            for (auto const& litesimphotons : (*opdetHandle)){
              channel = litesimphotons.OpChannel;
              if(channel==ch) auxLite =(litesimphotons);
              if(channel==(ch+2)) auxLite+=(litesimphotons);
 	    }
	    arapucaDigitizer->ConstructWaveformLite(ch, auxLite, waveform, map.pdName(ch),start_time*1000 /*ns for digitizer*/, n_samples);
            fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
          }
        }
      }  //end loop on simphoton lite collections
    }else{ //for SimPhotons
      std::map<int,sim::SimPhotons> auxmap;   // to temporarily store channel and direct light distribution
      //Get *ALL* SimPhotonsCollection from Event
      std::vector< art::Handle< std::vector< sim::SimPhotons > > > photon_handles;
      e.getManyByType(photon_handles);
      if (photon_handles.size() == 0)
	throw art::Exception(art::errors::ProductNotFound)<<"sim SimPhotons retrieved and you requested them.";
      
      CreateDirectPhotonMap(auxmap, photon_handles);
 
      for (auto opdetHandle: photon_handles) {
        bool Reflected = (opdetHandle.provenance()->productInstanceName() == "Reflected");

        std::cout << "Number of photon channels: " << opdetHandle->size() << std::endl;

        for (auto const& simphotons : (*opdetHandle)){
          std::vector<short unsigned int> waveform;
	  ch = simphotons.OpChannel();
	  if((Reflected) && (map.pdType(ch, "barepmt") || map.pdType(ch,"pmt"))){ //all PMTs
//	    std::cout << "PMT channels " <<std::endl;
	    pmtDigitizer->ConstructWaveform(ch, simphotons, waveform, map.pdName(ch), auxmap, start_time*1000 /*ns for digitizer*/, n_samples);
            fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
          }
/*	  if(map.pdType(ch, "bar")) //Paddles
	    std::cout << "Digitization not implemented for paddles. " <<std::endl;*/
	  if((map.pdType(ch, "arapucaT1") && !Reflected) || (map.pdType(ch, "arapucaT2") && Reflected) ){//getting only arapuca channels with appropriate type of light
//	    std::cout << "Arapuca channels " <<std::endl;
	    arapucaDigitizer->ConstructWaveform(ch, simphotons, waveform, map.pdName(ch),start_time*1000 /*ns for digitizer*/, n_samples);
            fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
          }
	  if((map.pdType(ch,"xarapucaprime") && !Reflected)){//getting only xarapuca channels with appropriate type of light (this separation is needed because xarapucas are set as two different optical channels but are actually only one readout channel)
//	    std::cout << "X-Arapuca channels " <<std::endl;
            sim::SimPhotons auxPhotons;
            for (auto const& simphotons : (*opdetHandle)){
              channel= simphotons.OpChannel();
              if(channel==ch) auxPhotons =(simphotons);
              if(channel==(ch+2)) auxPhotons+=(simphotons);
 	    }
	    arapucaDigitizer->ConstructWaveform(ch, auxPhotons, waveform, map.pdName(ch),start_time*1000 /*ns for digitizer*/, n_samples);
            fWaveforms.at(ch) = raw::OpDetWaveform(start_time, (unsigned int)ch, waveform);//including pre trigger window and transit time
          }
        }//optical channel loop
      }//type of light loop
    }//simphotons end
  }

  DEFINE_ART_MODULE(opdet::opDetDigitizerSBND)

  void opDetDigitizerSBND::CreateDirectPhotonMap(std::map<int,sim::SimPhotons>& auxmap, std::vector< art::Handle< std::vector< sim::SimPhotons > > > photon_handles)
  {
    int ch;
    // Loop over direct/reflected photons
    for (auto pmtHandle: photon_handles) {
       // Do some checking before we proceed
        if (!pmtHandle.isValid()) continue;  
        if (pmtHandle.provenance()->moduleLabel() != fInputModuleName) continue;   //not the most efficient way of doing this, but preserves the logic of the module. Andrzej
      //this now tells you if light collection is reflected
      bool Reflected = (pmtHandle.provenance()->productInstanceName() == "Reflected");
      
      for (auto const& simphotons : (*pmtHandle)){
	  ch = simphotons.OpChannel();
	  if(map.pdType(ch, "pmt") && !Reflected)
            auxmap.insert(std::make_pair(ch,simphotons));
      }
    }
  }

  void opDetDigitizerSBND::CreateDirectPhotonMapLite(std::map<int,sim::SimPhotonsLite>& auxmap, std::vector< art::Handle< std::vector< sim::SimPhotonsLite > > > photon_handles)
  {
    int ch;
    // Loop over direct/reflected photons
    for (auto pmtHandle: photon_handles) {
       // Do some checking before we proceed
        if (!pmtHandle.isValid()) continue;  
        if (pmtHandle.provenance()->moduleLabel() != fInputModuleName) continue;   //not the most efficient way of doing this, but preserves the logic of the module. Andrzej
      //this now tells you if light collection is reflected
      bool Reflected = (pmtHandle.provenance()->productInstanceName() == "Reflected");
      
      for (auto const& litesimphotons : (*pmtHandle)){
	  ch = litesimphotons.OpChannel;
	  if(map.pdType(ch, "pmt") && !Reflected)
            auxmap.insert(std::make_pair(ch,litesimphotons));
      }
    }
  }

}//closing namespace
