/**
 * @file   icaruscode/PMT/Algorithms/OpDetWaveformMetaUtils.cxx
 * @brief  Writes a collection of sbn::OpDetWaveformMeta from PMT waveforms.
 * @author Gianluca Petrillo (petrillo@slac.stanford.edu)
 * @date   November 22, 2021
 * @see    icaruscode/PMT/Algorithms/OpDetWaveformMetaUtils.h
 */


// library header
#include "icaruscode/PMT/Algorithms/OpDetWaveformMetaUtils.h"

// LArSoft libraries
#include "lardataalg/DetectorInfo/DetectorTimings.h"
#include "lardataobj/RawData/OpDetWaveform.h"


// -----------------------------------------------------------------------------
// ---  sbn::OpDetWaveformMetaMaker
// -----------------------------------------------------------------------------
sbn::OpDetWaveformMetaMaker::OpDetWaveformMetaMaker
  (detinfo::DetectorTimings const& detTimings)
  : fOpDetTickPeriod{ detTimings.OpticalClockPeriod() }
  , fTriggerTime{ detTimings.TriggerTime() }
  , fBeamGateTime{ detTimings.BeamGateTime() }
  {}


// -----------------------------------------------------------------------------
sbn::OpDetWaveformMetaMaker::OpDetWaveformMetaMaker(microseconds opDetTickPeriod)
  : fOpDetTickPeriod{ opDetTickPeriod }
  {}


// -----------------------------------------------------------------------------
sbn::OpDetWaveformMeta sbn::OpDetWaveformMetaMaker::make
  (raw::OpDetWaveform const& waveform) const
{
  
  using detinfo::timescales::electronics_time;
  
  raw::Channel_t const channel = waveform.ChannelNumber();
  std::size_t const nSamples = waveform.Waveform().size();
  electronics_time const startTime { waveform.TimeStamp() };
  electronics_time const endTime
    = startTime + waveform.Waveform().size() * fOpDetTickPeriod;
  
  sbn::OpDetWaveformMeta info {
      channel                   // channel
    , nSamples                  // nSamples
    , startTime.value()         // startTime
    , endTime.value()           // endTime
    /* the following are left default:
    // flags
    */
    };
  
  auto const setFlag = [&info]
    (sbn::OpDetWaveformMeta::Flags_t::Flag_t flag, bool value)
    { if (value) info.flags.set(flag); else info.flags.unset(flag); };
  
  auto const isInWaveform = [startTime,endTime](electronics_time t)
    { return (t >= startTime) && (t < endTime); };
  
  if (fTriggerTime) {
    setFlag
      (sbn::OpDetWaveformMeta::bits::WithTrigger, isInWaveform(*fTriggerTime));
  }
  
  if (fBeamGateTime) {
    setFlag
      (sbn::OpDetWaveformMeta::bits::WithBeamGate, isInWaveform(*fBeamGateTime));
  }
  
  return info;
  
} // sbn::OpDetWaveformMetaMaker::make()


// -----------------------------------------------------------------------------
// ---  functions
// -----------------------------------------------------------------------------
sbn::OpDetWaveformMeta sbn::makeOpDetWaveformMeta(
  raw::OpDetWaveform const& waveform,
  detinfo::DetectorTimings const& detTimings
) {
  return sbn::OpDetWaveformMetaMaker{ detTimings }.make(waveform);
}


sbn::OpDetWaveformMeta sbn::makeOpDetWaveformMeta(
  raw::OpDetWaveform const& waveform,
  util::quantities::intervals::microseconds opDetTickPeriod
) {
  return sbn::OpDetWaveformMetaMaker{ opDetTickPeriod }.make(waveform);
}


// -----------------------------------------------------------------------------
