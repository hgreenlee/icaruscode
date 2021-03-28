/**
 * @file   SlidingWindowTriggerSimulation_module.cc
 * @brief  Plots of efficiency for triggers based on PMT sliding windows.
 * @author Gianluca Petrillo (petrillo@slac.stanford.edu)
 * @date   March 27, 2021
 */


// ICARUS libraries
#include "icaruscode/PMT/Trigger/Algorithms/SlidingWindowPatternAlg.h"
#include "icaruscode/PMT/Trigger/Algorithms/WindowTopologyAlg.h" // WindowTopologyManager
#include "icaruscode/PMT/Trigger/Algorithms/WindowPatternConfig.h"
#include "icaruscode/PMT/Trigger/Algorithms/WindowPattern.h"
#include "icaruscode/PMT/Trigger/Algorithms/ApplyBeamGate.h"
#include "icaruscode/PMT/Trigger/Algorithms/BeamGateMaker.h"
#include "icaruscode/PMT/Trigger/Algorithms/TriggerTypes.h" // ADCCounts_t
#include "icaruscode/PMT/Trigger/Algorithms/details/TriggerInfo_t.h"
#include "sbnobj/ICARUS/PMT/Trigger/Data/MultiChannelOpticalTriggerGate.h"
#include "sbnobj/ICARUS/PMT/Trigger/Data/OpticalTriggerGate.h"
#include "icaruscode/PMT/Trigger/Utilities/TriggerDataUtils.h" // FillTriggerGates()
#include "icaruscode/PMT/Trigger/Utilities/PlotSandbox.h"
#include "icarusalg/Utilities/ROOTutils.h" // util::ROOT
#include "icaruscode/Utilities/DetectorClocksHelpers.h" // makeDetTimings()...
#include "icarusalg/Utilities/mfLoggingClass.h"
#include "icarusalg/Utilities/ChangeMonitor.h" // ThreadSafeChangeMonitor
#include "icarusalg/Utilities/rounding.h" // icarus::ns::util::roundup()

// LArSoft libraries
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "larcore/Geometry/Geometry.h"
#include "larcore/CoreUtils/ServiceUtil.h" // lar::providerFrom()
#include "lardataalg/DetectorInfo/DetectorTimings.h"
#include "lardataalg/DetectorInfo/DetectorClocks.h"
#include "lardataalg/DetectorInfo/DetectorTimingTypes.h" // optical_tick...
#include "lardataalg/Utilities/quantities/spacetime.h" // microseconds, ...
#include "lardataalg/Utilities/intervals_fhicl.h" // microseconds from FHiCL
#include "larcorealg/Geometry/GeometryCore.h"
#include "larcorealg/CoreUtils/counter.h"
#include "larcorealg/CoreUtils/enumerate.h"
#include "larcorealg/CoreUtils/values.h" // util::const_values()
#include "larcorealg/CoreUtils/get_elements.h" // util::get_elements()
#include "larcorealg/CoreUtils/UncopiableAndUnmovableClass.h"
#include "larcorealg/CoreUtils/StdUtils.h" // util::to_string()
#include "lardataobj/RawData/TriggerData.h" // raw::Trigger
#include "lardataobj/RawData/OpDetWaveform.h" // raw::ADC_Count_t
#include "larcoreobj/SimpleTypesAndConstants/geo_types.h" // geo::CryostatID

// framework libraries
#include "art_root_io/TFileService.h"
#include "art_root_io/TFileDirectory.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "canvas/Utilities/InputTag.h"
#include "canvas/Utilities/Exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "fhiclcpp/types/Sequence.h"
#include "fhiclcpp/types/Atom.h"

// ROOT libraries
#include "TEfficiency.h"
#include "TH1F.h"
#include "TH2F.h"

// C/C++ standard libraries
#include <ostream>
#include <algorithm> // std::fill()
#include <map>
#include <vector>
#include <memory> // std::make_unique()
#include <string>
#include <atomic>
#include <optional>
#include <utility> // std::pair<>, std::move()
#include <cmath> // std::ceil()
#include <cstddef> // std::size_t
#include <cassert>


//------------------------------------------------------------------------------
using namespace util::quantities::time_literals;


//------------------------------------------------------------------------------
namespace icarus::trigger { class SlidingWindowTriggerSimulation; }
/**
 * @brief Simulates a sliding window trigger.
 * 
 * This module produces `raw::Trigger` objects each representing the outcome of
 * some trigger logic applied to a discriminated input ("trigger primitives").
 * 
 * A trigger primitive is a two-level function of time which describes when
 * that primitive is on and when it is off. Trigger primitives are given as
 * input to this module and their origin may vary, but the standard source in
 * ICARUS is @ref ICARUSPMTTriggerGlossary "single trigger request (LVDS)".
 * 
 * This module applies a sliding window pattern to the input: the pattern
 * consists of a requirement on the main window and optional additional
 * requirements on the neighbouring windows. This module rebases the configured
 * pattern on each of the available windows, evaluates the requirement of the
 * pattern in that configuration, and decides whether those requirements are
 * met. The general trigger is considered passed if _any_ of the rebased
 * patterns satisfies the requirement at any time, and no special treatment is
 * performed in case multiple windows fulfil them, except that the trigger time
 * is driven by the earliest of the satisfied patterns.
 * 
 * A single trigger pattern is configured for each instance of the module,
 * while multiple input sets (e.g. with different discrimination thresholds)
 * can be processed on the same pattern by the same module instance.
 * Conversely, testing a different pattern requires the instantiation of a new
 * module.
 * 
 * 
 * Configuration
 * ==============
 * 
 * * `TriggerGatesTag` (string, mandatory): name of the module instance which
 *     produced the trigger primitives to be used as input; it must not include
 *     any instance name, as the instance names will be automatically added from
 *     `Thresholds` parameter.
 *     The typical trigger primitives used as input are LVDS discriminated
 *     output combined into trigger windows (e.g. from
 *     `icarus::trigger::SlidingWindowTrigger` module).
 * * `Thresholds` (list of names, mandatory): list of the discrimination
 *     thresholds to consider. A data product containing a digital signal is
 *     read for each one of the thresholds, and the tag of the data product is
 *     expected to be the instance name in this configuration parameter for the
 *     module label set in `TriggerGatesTag` (e.g. for a threshold of
 *     `"60"`, supposedly 60 ADC counts, and with `TriggerGatesTag` set to
 *     `"TrigSlidingWindows"`, the data product tag would be
 *     `TrigSlidingWindows:60`).
 * * `Pattern` (configuration table, mandatory): describes the sliding window
 *     pattern; the configuration format for a pattern is described under
 *     `icarus::trigger::ns::fhicl::WindowPatternConfig`.
 * * `BeamGateDuration` (time, _mandatory_): the duration of the beam
 *     gate; _the time requires the unit to be explicitly specified_: use
 *     `"1.6 us"` for BNB, `9.5 us` for NuMI (also available as
 *     `BNB_settings.spill_duration` and `NuMI_settings.spill_duration` in
 *     `trigger_icarus.fcl`);
 * * `BeamBits` (TODO): bits to be set in the produced `raw::Trigger` objects.
 * * `LogCategory` (string, default `SlidingWindowTriggerSimulation`): name of
 *     category used to stream messages from this module into message facility.
 * 
 * An example job configuration is provided as
 * `simulate_sliding_window_trigger_icarus.fcl`.
 * 
 * 
 * Output data products
 * =====================
 * 
 * * `std::vector<raw::Trigger>` (one instance per ADC threshold):
 *   list of triggers fired according to the configured trigger definition;
 *   there is one collection (and data product) per ADC threshold, and the
 *   data product has the same instance name as the input data one
 *   (see `TriggerGatesTag` and `Thresholds` configuration parameters);
 *   currently only at most one trigger is emitted, with time stamp matching
 *   the first time the trigger criteria are satisfied.
 * 
 * 
 * 
 * Trigger logic algorithm
 * ========================
 * 
 * @anchor SlidingWindowTriggerSimulation_Algorithm
 * 
 * This section describes the trigger logic algorithm used in
 * `icarus::trigger::SlidingWindowTriggerSimulation` and its assumptions.
 * Nevertheless, more up-to-date information can be found in
 * `SlidingWindowTrigger` module (for the combination of the LVDS signals into
 * window-wide gates) and in `icarus::trigger::SlidingWindowPatternAlg`,
 * which applies the configured pattern logic to the input.
 * 
 * The module receives as input a multi-level trigger gate for each of the
 * windows to be considered.
 * On the first input (i.e. the first event), that input is parsed to learn
 * the windows and their relative position from the input trigger gates.
 * This topology will be used to apply the configured patterns. On the following
 * events, their input is checked to confirm the compatibility of the
 * composition of its windows with the one from that first event (both aspects
 * are handled by an `icarus::trigger::WindowTopologyManager` object).
 * 
 * All multi-level gates are set in coincidence with the beam gate by
 * multiplying the multi-level and the beam gates. Because of this, trigger
 * gates are suppressed everywhere except than during the beam gate.
 * The beam gate opens at a time configured in `DetectorClocks` service provider
 * (`detinfo::DetectorClocks::BeamGateTime()`), optionally offset
 * (`BeamGateStart`), and has a duration configured in this module
 * (`BeamGateDuration`).
 * 
 * The algorithm handles independently multiple trigger patterns.
 * On each input, each configured pattern is applied based on the window
 * topology. Each pattern describes a minimum level of the trigger
 * gate in the window, that usually means the number of LVDS signals in
 * coincidence at any given time ("majority"). A pattern may have requirements
 * on the neighbouring windows in addition to the main one. The pattern is
 * satisfied if all involved windows pass their specific requirements at the
 * same time (coincidence between windows).
 * Each pattern is applied in turn to each of the windows (which is the "main"
 * window). The neighborhood described in the pattern is applied with respect to
 * that main window. The trigger fires if one or more of the windows satisfy
 * the pattern, and the trigger time is the one of the earliest satisfied
 * pattern (more precisely, the earliest tick when the coincidence required
 * by that pattern is satisfied).* 
 * All windows in the detector are considered independently, but the supported
 * patterns may only include components in the same cryostat. Therefore,
 * triggers are effectively on a single cryostat.
 * An object of class `icarus::trigger::SlidingWindowPatternAlg` applies this
 * logic: see its documentation for the most up-to-date details.
 * 
 * Eventually, for each event there are as many different trigger responses as
 * how many different patterns are configured (`Patterns` configuration
 * parameter), _times_ how many ADC thresholds are provided in input,
 * configured in `Thresholds`.
 * 
 * 
 * Technical aspects of the module
 * --------------------------------
 * 
 * @anchor SlidingWindowTriggerSimulation_Tech
 * 
 * This module does not build the trigger gates of the sliding windows, but
 * rather it takes them as input (see e.g. `SlidingWindowTrigger` module).
 * Window topology (size of the windows and their relations) is stored in
 * `icarus::trigger::WindowChannelMap` objects, and its construction is
 * delegated to `icarus::trigger::WindowTopologyAlg` (under the hood of the
 * `WindowTopologyManager` class) which learns it from the actual trigger gate
 * input rather than on explicit configuration. Pattern definitions and
 * configuration are defined in `icarus::trigger::WindowPattern` and
 * `icarus::trigger::ns::fhicl::WindowPatternConfig` respectively. Trigger
 * simulation is delegated to `icarus::trigger::SlidingWindowPatternAlg`.
 * 
 */
class icarus::trigger::SlidingWindowTriggerSimulation
  : public art::EDProducer
  , private lar::UncopiableAndUnmovableClass
{

    public:
  
  using microseconds = util::quantities::intervals::microseconds;
  using nanoseconds = util::quantities::intervals::nanoseconds;
  
  
  // --- BEGIN Configuration ---------------------------------------------------
  struct Config {
    
    using Name = fhicl::Name;
    using Comment = fhicl::Comment;
    
    fhicl::Atom<std::string> TriggerGatesTag {
      Name("TriggerGatesTag"),
      Comment("label of the input trigger gate data product (no instance name)")
      };

    fhicl::Sequence<std::string> Thresholds {
      Name("Thresholds"),
      Comment("tags of the thresholds to consider")
      };

    icarus::trigger::ns::fhicl::WindowPatternTable Pattern {
      Name("Pattern"),
      Comment("trigger requirements as a trigger window pattern")
      };
 
    fhicl::Atom<microseconds> BeamGateDuration {
      Name("BeamGateDuration"),
      Comment("length of time interval when optical triggers are accepted")
      };

    fhicl::Atom<std::uint32_t> BeamBits {
      Name("BeamBits"),
      Comment("bits to be set in the trigger object as beam identified")
      };

    fhicl::Atom<nanoseconds> TriggerTimeResolution {
      Name("TriggerTimeResolution"),
      Comment("resolution of trigger in time"),
      25_ns
      };
    
    fhicl::Atom<std::string> LogCategory {
      Name("LogCategory"),
      Comment("name of the category used for the output"),
      "SlidingWindowTriggerSimulation" // default
      };
    
  }; // struct Config

  using Parameters = art::EDProducer::Table<Config>;
  // --- END Configuration -----------------------------------------------------


  // --- BEGIN Constructors ----------------------------------------------------
  explicit SlidingWindowTriggerSimulation(Parameters const& config);

  // --- END Constructors ------------------------------------------------------


  // --- BEGIN Framework hooks -------------------------------------------------

  /// Initializes the plots.
  virtual void beginJob() override;
  
  /// Runs the simulation and saves the results into the _art_ event.
  virtual void produce(art::Event& event) override;
  
  /// Prints end-of-job summaries.
  virtual void endJob() override;
  
  // --- END Framework hooks ---------------------------------------------------
  
  
    private:
  
  using TriggerInfo_t = details::TriggerInfo_t; ///< Type alias.
  
  /// Type of trigger gate extracted from the input event.
  using InputTriggerGate_t
    = icarus::trigger::SlidingWindowPatternAlg::InputTriggerGate_t;
  
  /// List of trigger gates.
  using TriggerGates_t
    = icarus::trigger::SlidingWindowPatternAlg::TriggerGates_t;
  
  /// Data structure to communicate internally a trigger response.
  using WindowTriggerInfo_t
    = icarus::trigger::SlidingWindowPatternAlg::AllTriggerInfo_t;
  
  
  // --- BEGIN Configuration variables -----------------------------------------
  
  /// Name of ADC thresholds to read, and the input tag connected to their data.
  std::map<std::string, art::InputTag> fADCthresholds;
  
  /// Configured sliding window requirement pattern.
  WindowPattern const fPattern;
  
  /// Duration of the gate during with global optical triggers are accepted.
  microseconds fBeamGateDuration;
  
  std::uint32_t fBeamBits; ///< Bits for the beam gate being simulated.
  
  nanoseconds fTriggerTimeResolution; ///< Trigger resolution in time.
  
  /// Message facility stream category for output.
  std::string const fLogCategory;
  
  // --- END Configuration variables -------------------------------------------
  
  
  // --- BEGIN Service variables -----------------------------------------------

  /// ROOT directory where all the plots are written.
  art::TFileDirectory fOutputDir;

  // --- END Service variables -------------------------------------------------

  
  // --- BEGIN Internal variables ----------------------------------------------
  
  /// Mapping of each sliding window with location and topological information.
  // mutable = not thread-safe
  mutable icarus::trigger::WindowTopologyManager fWindowMapMan;
  
  /// Pattern algorithm.
  std::optional<icarus::trigger::SlidingWindowPatternAlg> fPatternAlg;
  
  /// All plots in one practical sandbox.
  icarus::trigger::PlotSandbox fPlots;

  ///< Count of fired triggers, per threshold.
  std::vector<std::atomic<unsigned int>> fTriggerCount;
  std::atomic<unsigned int> fTotalEvents { 0U }; ///< Count of fired triggers.
  
  
  /// Functor returning whether a gate has changed.
  icarus::ns::util::ThreadSafeChangeMonitor<icarus::trigger::ApplyBeamGateClass>
    fGateChangeCheck;

  // --- END Internal variables ------------------------------------------------
  

  
  // --- BEGIN Derived class methods -------------------------------------------
  
  /// @brief Initializes the full set of plots (all ADC thresholds).
  void initializePlots();
  
  /**
   * @brief Performs the simulation for the specified ADC threshold.
   * @param event _art_ event to read data from and put results into
   * @param iThr index of the threshold in the configuration
   * @param thr value of the threshold (ADC counts)
   * @return the trigger response information
   * 
   * For the given threshold, the simulation of the configured trigger is
   * performed.
   * The input data is read from the event (the source tag is from the module
   * configuration), simulation is performed, auxiliary plots are drawn and
   * a `raw::Trigger` collection is stored into the event.
   * 
   * The stored collection contains either one or zero `raw::Trigger` elements.
   * 
   * The simulation itself is performed by the `simulate()` method.
   */
  WindowTriggerInfo_t produceForThreshold(
    art::Event& event,
    detinfo::DetectorTimings const& detTimings,
    ApplyBeamGateClass const& beamGate,
    std::size_t const iThr, std::string const& thrTag
    );
  
  /**
   * @brief Converts the trigger information into a `raw::Trigger` object.
   * @param triggerNumber the unique number to assign to this trigger
   * @param info the information about the fired trigger
   * @return a `raw::Trigger` object with all the information encoded
   * 
   * The trigger described by `info` is encoded into a `raw::Trigger` object.
   * The trigger _must_ have fired.
   */
  raw::Trigger triggerInfoToTriggerData
    (detinfo::DetectorTimings const& detTimings,
     unsigned int triggerNumber, WindowTriggerInfo_t const& info) const;
  
  /// Fills the plots for threshold index `iThr` with trigger information.
  void plotTriggerResponse
    (std::size_t iThr, WindowTriggerInfo_t const& triggerInfo) const;
  
  
  /// Prints the summary of fired triggers on screen.
  void printSummary() const;
  
  
  //@{ 
  /// Shortcut to create an `ApplyBeamGate` with the current configuration.
  icarus::trigger::ApplyBeamGateClass makeMyBeamGate
    (art::Event const* event = nullptr) const
    {
      return makeApplyBeamGate(
        fBeamGateDuration,
        icarus::ns::util::makeDetClockData(event),
        fLogCategory
        );
    }
  icarus::trigger::ApplyBeamGateClass makeMyBeamGate
    (art::Event const& event) const { return makeMyBeamGate(&event); }
  //@}
  
  
  /// Reads a set of input gates from the `event`
  /// @return trigger gates, converted into `InputTriggerGate_t`
  static TriggerGates_t readTriggerGates
    (art::Event const& event, art::InputTag const& dataTag);
  

}; // icarus::trigger::SlidingWindowTriggerSimulation



//------------------------------------------------------------------------------
//--- Implementation
//------------------------------------------------------------------------------
icarus::trigger::SlidingWindowTriggerSimulation::SlidingWindowTriggerSimulation
  (Parameters const& config)
  : art::EDProducer       (config)
  // configuration
  , fPattern              (config().Pattern())
  , fBeamGateDuration     (config().BeamGateDuration())
  , fBeamBits             (config().BeamBits())
  , fTriggerTimeResolution(config().TriggerTimeResolution())
  , fLogCategory          (config().LogCategory())
  // services
  , fOutputDir (*art::ServiceHandle<art::TFileService>())
  // internal and cached
  , fWindowMapMan
    { *lar::providerFrom<geo::Geometry>(), fLogCategory + ":WindowMapManager" }
  , fPlots(
     fOutputDir, "", "requirement: " + fPattern.description()
    )
{
  
  //
  // more complex parameter parsing
  //
  std::string const& discrModuleLabel = config().TriggerGatesTag();
  for (std::string const& threshold: config().Thresholds())
    fADCthresholds[threshold] = art::InputTag{ discrModuleLabel, threshold };
  
  // initialization of a vector of atomic is not as trivial as it sounds...
  fTriggerCount = std::vector<std::atomic<unsigned int>>(fADCthresholds.size());
  std::fill(fTriggerCount.begin(), fTriggerCount.end(), 0U);

  //
  // input data declaration
  //
  using icarus::trigger::OpticalTriggerGateData_t; // for convenience

  // trigger primitives
  for (art::InputTag const& inputDataTag: util::const_values(fADCthresholds)) {
    consumes<std::vector<OpticalTriggerGateData_t>>(inputDataTag);
    consumes<art::Assns<OpticalTriggerGateData_t, raw::OpDetWaveform>>
      (inputDataTag);
  } // for
  
  //
  // output data declaration
  //
  for (art::InputTag const& inputDataTag: util::const_values(fADCthresholds))
    produces<std::vector<raw::Trigger>>(inputDataTag.instance());
  
  {
    mf::LogInfo log(fLogCategory);
    log << "\nConfigured " << fADCthresholds.size() << " thresholds (ADC):";
    for (auto const& [ thresholdTag, dataTag ]: fADCthresholds)
      log << "\n * " << thresholdTag << " (from '" << dataTag.encode() << "')";
    
  } // local block
  
  
} // icarus::trigger::SlidingWindowTriggerSimulation::SlidingWindowTriggerSimulation()


//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::beginJob() {
  
  initializePlots();
  
} // icarus::trigger::SlidingWindowTriggerSimulation::beginJob()


//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::produce(art::Event& event)
{
  
  auto const clockData
    = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(event);
  detinfo::DetectorTimings const detTimings{clockData};
  auto const beamGate = makeMyBeamGate(event);

  if (auto oldGate = fGateChangeCheck(beamGate); oldGate) {
    mf::LogWarning(fLogCategory)
      << "Beam gate has changed from " << *oldGate << " to " << beamGate << "!";
  }


  mf::LogDebug log(fLogCategory); // this will print at the end of produce()
  log << "Event " << event.id() << ":";
  
  for (auto const& [ iThr, thrTag ]
    : util::enumerate(util::get_elements<0U>(fADCthresholds))
  ) {
    
    WindowTriggerInfo_t const triggerInfo
      = produceForThreshold(event, detTimings, beamGate, iThr, thrTag);
    
    log << "\n * threshold " << thrTag << ": ";
    if (triggerInfo) log << "trigger at " << triggerInfo.info.atTick();
    else             log << "not triggered";
    
  } // for
  
  ++fTotalEvents;
  
} // icarus::trigger::SlidingWindowTriggerSimulation::produce()


//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::endJob() {
  
  printSummary();
  
} // icarus::trigger::SlidingWindowTriggerSimulation::endJob()


//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::initializePlots() {
  
  //
  // overview plots with different settings
  //
  
  std::vector<std::string> thresholdLabels;
  thresholdLabels.reserve(size(fADCthresholds));
  for (std::string thr: util::get_elements<0U>(fADCthresholds))
    thresholdLabels.push_back(std::move(thr));
  
  auto const beamGate = makeMyBeamGate();
  fGateChangeCheck(beamGate);
  mf::LogInfo(fLogCategory)
    << "Beam gate for plots: " << beamGate.asSimulationTime()
    << " (simulation time), " << beamGate.tickRange()
    << " (optical ticks)"
    ;

  //
  // Triggering efficiency vs. ADC threshold.
  //
  auto* NTriggers = fPlots.make<TH1F>(
    "NTriggers",
    "Number of triggering events"
      ";PMT discrimination threshold  [ ADC counts ]"
      ";events",
    thresholdLabels.size(), 0.0, double(thresholdLabels.size())
    );
  util::ROOT::applyAxisLabels(NTriggers->GetXaxis(), thresholdLabels);
  
  auto* Eff = fPlots.make<TEfficiency>(
    "Eff",
    "Triggering pass fraction"
      ";PMT discrimination threshold  [ ADC counts ]"
      ";trigger pass fraction",
    thresholdLabels.size(), 0.0, double(thresholdLabels.size())
    );
  // people are said to have earned hell for things like this;
  // but TEfficiency really does not expose the interface to assign labels to
  // its axes, which supposedly could be done had we chosen to create it by
  // histograms instead of directly as recommended.
  util::ROOT::applyAxisLabels
    (const_cast<TH1*>(Eff->GetTotalHistogram())->GetXaxis(), thresholdLabels);
  
  detinfo::timescales::optical_time_ticks const triggerResolutionTicks{
    icarus::ns::util::makeDetTimings().toOpticalTicks(fTriggerTimeResolution)
    };
  
  auto const& beamGateTicks = beamGate.tickRange();
  auto* TrigTime = fPlots.make<TH2F>(
    "TriggerTick",
    "Trigger time tick"
      ";optical time tick [ /" + util::to_string(triggerResolutionTicks) + " ]"
      ";PMT discrimination threshold  [ ADC counts ]"
      ";events",
    static_cast<int>(std::ceil(beamGate.lengthTicks()/triggerResolutionTicks)),
    beamGateTicks.start().value(),
    icarus::ns::util::roundup
     (beamGateTicks.start() + beamGate.lengthTicks(), triggerResolutionTicks)
     .value(),
    thresholdLabels.size(), 0.0, double(thresholdLabels.size())
    );
  util::ROOT::applyAxisLabels(TrigTime->GetYaxis(), thresholdLabels);
  
  
} // icarus::trigger::SlidingWindowTriggerSimulation::initializePlots()


//------------------------------------------------------------------------------
auto icarus::trigger::SlidingWindowTriggerSimulation::produceForThreshold(
  art::Event& event,
  detinfo::DetectorTimings const& detTimings,
  ApplyBeamGateClass const& beamGate,
  std::size_t const iThr, std::string const& thrTag
) -> WindowTriggerInfo_t {
  
  //
  // get the input
  //
  art::InputTag const& dataTag = fADCthresholds.at(thrTag);
  auto const& gates = readTriggerGates(event, dataTag);
  
  // extract or verify the topology of the trigger windows
  if (fWindowMapMan(gates))
    fPatternAlg.emplace(*fWindowMapMan, fPattern, fLogCategory);
  assert(fPatternAlg);
  
  //
  // simulate the trigger response
  //
  WindowTriggerInfo_t const triggerInfo
    = fPatternAlg->simulateResponse(beamGate.applyToAll(gates));
  if (triggerInfo) ++fTriggerCount[iThr]; // keep the unique count
  
  
  //
  // fill the plots
  //
  plotTriggerResponse(iThr, triggerInfo);

  //
  // create and store the data product
  //
  auto triggers = std::make_unique<std::vector<raw::Trigger>>();
  if (triggerInfo.info.fired()) {
    triggers->push_back
      (triggerInfoToTriggerData(detTimings, fTriggerCount[iThr], triggerInfo));
  } // if
  event.put(std::move(triggers), dataTag.instance());
  
  return triggerInfo;
  
} // icarus::trigger::SlidingWindowTriggerSimulation::produceForThreshold()



//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::plotTriggerResponse
  (std::size_t iThr, WindowTriggerInfo_t const& triggerInfo) const
{
  bool const fired = triggerInfo.info.fired();
  
  fPlots.demand<TEfficiency>("Eff").Fill(fired, iThr);
  
  if (fired) {
    fPlots.demand<TH1>("NTriggers").Fill(iThr);
    fPlots.demand<TH2>("TriggerTick").Fill
      (triggerInfo.info.atTick().value(), iThr);
  }
  
} // icarus::trigger::SlidingWindowTriggerSimulation::plotTriggerResponse()


//------------------------------------------------------------------------------
void icarus::trigger::SlidingWindowTriggerSimulation::printSummary() const {
  
  //
  // summary from our internal counters
  //
  mf::LogInfo log(fLogCategory);
  log
    << "Summary of triggers for " << fTriggerCount.size()
    << " thresholds (ADC) with pattern: " << fPattern.description()
    ;
  for (auto const& [ count, thr ]
    : util::zip(fTriggerCount, util::get_elements<0U>(fADCthresholds)))
  {
    log << "\n  threshold " << thr
      << ": " << count;
    if (fTotalEvents > 0U) {
      log << "/" << fTotalEvents
        << " (" << (double(count) / fTotalEvents * 100.0) << "%)";
    }
    else log << " events triggered";
  } // for
  
} // icarus::trigger::SlidingWindowTriggerSimulation::printSummary()


//------------------------------------------------------------------------------
raw::Trigger
icarus::trigger::SlidingWindowTriggerSimulation::triggerInfoToTriggerData
  (detinfo::DetectorTimings const& detTimings,
   unsigned int triggerNumber, WindowTriggerInfo_t const& info) const
{
  assert(info.info.fired());
  
  return {
    triggerNumber,                                            // counter
    double(detTimings.toElectronicsTime(info.info.atTick())), // trigger time
    double(detTimings.BeamGateTime()), // beam gate in electronics time scale
    fBeamBits                                                 // bits 
    };
  
} // icarus::trigger::SlidingWindowTriggerSimulation::triggerInfoToTriggerData()


//------------------------------------------------------------------------------
auto icarus::trigger::SlidingWindowTriggerSimulation::readTriggerGates
  (art::Event const& event, art::InputTag const& dataTag)
  -> TriggerGates_t
{

  using icarus::trigger::OpticalTriggerGateData_t; // for convenience

  // currently the associations are a waste of time memory...
  auto const& gates
    = *(event.getValidHandle<std::vector<OpticalTriggerGateData_t>>(dataTag));
  auto const& gateToWaveforms = *(
    event.getValidHandle
      <art::Assns<OpticalTriggerGateData_t, raw::OpDetWaveform>>(dataTag)
    );
  
  try {
    return icarus::trigger::FillTriggerGates<InputTriggerGate_t>
      (gates, gateToWaveforms);
  }
  catch (cet::exception const& e) {
    throw cet::exception("SlidingWindowTriggerSimulation", "", e)
      << "Error encountered while reading data products from '"
      << dataTag.encode() << "'\n";
  }

} // icarus::trigger::SlidingWindowTriggerSimulation::readTriggerGates()


//------------------------------------------------------------------------------
DEFINE_ART_MODULE(icarus::trigger::SlidingWindowTriggerSimulation)


//------------------------------------------------------------------------------
