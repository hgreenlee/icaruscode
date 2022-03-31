/**
 * @file   icaruscode/PMT/Trigger/Algorithms/ManagedTriggerGateBuilder.h
 * @brief  Algorithm to produce trigger gates out of optical readout waveforms.
 * @author Gianluca Petrillo (petrillo@slac.stanford.edu)
 * @date   April 1, 2019
 * @see    `icaruscode/PMT/Trigger/Algorithms/ManagedTriggerGateBuilder.tcc`
 * 
 */

#ifndef ICARUSCODE_PMT_TRIGGER_ALGORITHMS_MANAGEDTRIGGERGATEBUILDER_H
#define ICARUSCODE_PMT_TRIGGER_ALGORITHMS_MANAGEDTRIGGERGATEBUILDER_H


// ICARUS libraries
#include "icaruscode/PMT/Trigger/Algorithms/TriggerGateBuilder.h"

// LArSoft libraries
#include "lardataobj/RawData/OpDetWaveform.h"

// C/C++ standard libraries
#include <vector>


namespace icarus::trigger {
  
  // ---------------------------------------------------------------------------
  //
  // declarations
  //
  
  class ManagedTriggerGateBuilder;
  
  // ---------------------------------------------------------------------------
  
} // namespace icarus::trigger


//------------------------------------------------------------------------------
/**
 * @brief Base interface for gate builders.
 * @see `icarus::trigger::DynamicTriggerGateBuilder`,
 *      `icarus::trigger::FixedTriggerGateBuilder`
 * 
 * This base class provides a skeleton building algorithm that can be customised
 * by templates.
 * The allowed customization includes what to do when a threshold is crossed
 * in a gate.
 * 
 * Note that actions are performed only when the sample crosses a threshold.
 * The algorithm keeps track at each time of which are the thresholds enclosing
 * the signal level, and if the level crosses one of them, the gates associated
 * to those thresholds, and only them, are offered a chance to react.
 */
class icarus::trigger::ManagedTriggerGateBuilder
  : public icarus::trigger::TriggerGateBuilder
{
  using Base_t = icarus::trigger::TriggerGateBuilder;
  
    public:
  
  using Base_t::Base_t;
  
    protected:
  
  // This class describes the interface of a gate manager but is incomplete.
  struct GateManager {
    
      protected:
    struct GateInfoBase {
      // OpticalTriggerGateData_t:
      using TriggerGate_t = Base_t::TriggerGates::triggergate_t;
      // icarus::trigger::ReadoutTriggerGate:
      using TriggerGateData_t = TriggerGate_t::TriggerGate_t;
      
      TriggerGate_t* pGate = nullptr; ///< Pointer to the gate.
      
      GateInfoBase(TriggerGate_t& gate): pGate(&gate) {}
      TriggerGateData_t* operator->() const { return &gate(); }
      TriggerGateData_t& gate() const { return pGate->gate(); }
      void addTrackingInfo(raw::OpDetWaveform const& waveform) const
        { return pGate->tracking().add(&waveform); }
      
      void belowThresholdAt(optical_tick tick); // undefined: don't call!
      void aboveThresholdAt(optical_tick tick); // undefined: don't call!
      
    }; // struct GateInfoBase
    
      public:
    using GateInfo_t = GateInfoBase;
    
    GateInfo_t create(GateInfo_t::TriggerGate_t& gate) const;
    
  }; // struct GateManager
  
  
  /// Returns a collection of `TriggerGates` objects sorted by threshold.
  template <typename GateMgr>
  std::vector<TriggerGates> unifiedBuild
    (GateMgr&& gateManager, std::vector<WaveformWithBaseline> const& waveforms)
    const;
  
  /// Computes the gates for all the waveforms in one optical channel.
  template <typename GateInfo, typename Waveforms>
  void buildChannelGates
    (std::vector<GateInfo>& channelGates, Waveforms const& channelWaveforms)
    const;
  
}; // class icarus::trigger::ManagedTriggerGateBuilder


//------------------------------------------------------------------------------
//--- template implementation
//------------------------------------------------------------------------------
#include "icaruscode/PMT/Trigger/Algorithms/ManagedTriggerGateBuilder.tcc"

//------------------------------------------------------------------------------

#endif // ICARUSCODE_PMT_TRIGGER_ALGORITHMS_MANAGEDTRIGGERGATEBUILDER_H
