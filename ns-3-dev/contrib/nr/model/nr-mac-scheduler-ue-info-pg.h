/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#pragma once

#include "nr-mac-scheduler-ue-info-rr.h"

namespace ns3 {

class NrMacSchedulerUeInfoPG : public NrMacSchedulerUeInfo
{
public:
  NrMacSchedulerUeInfoPG (uint16_t rnti, BeamConfId beamConfId, const GetRbPerRbgFn &fn)
   : NrMacSchedulerUeInfo (rnti, beamConfId, fn), m_priority (1)
  {
  }

  virtual void ResetDlSchedInfo () override
  {
    NrMacSchedulerUeInfo::ResetDlSchedInfo ();
  }

  virtual void ResetDlMetric () override
  {
     NrMacSchedulerUeInfo::ResetDlMetric ();
  }

  virtual void ResetUlSchedInfo () override
  {
    NrMacSchedulerUeInfo::ResetUlSchedInfo ();
  }

  virtual void ResetUlMetric () override
  {
    NrMacSchedulerUeInfo::ResetUlMetric ();
  }

  void UpdatePriority(uint16_t Priority)
  {
    m_priority = Priority;
  }

  void UpdateDlPGMetric (const NrMacSchedulerNs3::FTResources &totAssigned,
                         const Ptr<const NrAmc> &amc);

  void UpdateUlPGMetric (const NrMacSchedulerNs3::FTResources &totAssigned,
                         const Ptr<const NrAmc> &amc);

  // void CalculatePotentialTPutDl (const NrMacSchedulerNs3::FTResources &assignableInIteration,
  //                                const Ptr<const NrAmc> &amc);

  // void CalculatePotentialTPutUl (const NrMacSchedulerNs3::FTResources &assignableInIteration,
  //                              const Ptr<const NrAmc> &amc);

  static bool CompareUeWeightsDl (const NrMacSchedulerNs3::UePtrAndBufferReq &lue,
                                  const NrMacSchedulerNs3::UePtrAndBufferReq & rue)
  {
    auto luePtr = dynamic_cast<NrMacSchedulerUeInfoPG*>(lue.first.get());
    auto ruePtr = dynamic_cast<NrMacSchedulerUeInfoPG*>(rue.first.get());
    
    double lPfMetric = luePtr->m_priority;
    double rPfMetric = ruePtr->m_priority;

    return lPfMetric > rPfMetric;
  }

  static bool CompareUeWeightsUl (const NrMacSchedulerNs3::UePtrAndBufferReq &lue,
                                  const NrMacSchedulerNs3::UePtrAndBufferReq & rue)
  {
    auto luePtr = dynamic_cast<NrMacSchedulerUeInfoPG*>(lue.first.get());
    auto ruePtr = dynamic_cast<NrMacSchedulerUeInfoPG*>(rue.first.get());

    double lPfMetric = luePtr->m_priority;
    double rPfMetric = ruePtr->m_priority;

    return lPfMetric > rPfMetric;
  }

  double m_potentialTputDl {0.0};
  double m_avgTputDl  {0.0};

  double m_potentialTputUl {0.0};
  double m_avgTputUl  {0.0};

  uint64_t m_priority {1};
};

} // namespace ns3
