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

#include "nr-mac-scheduler-ue-info-ag.h"
#include <ns3/log.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NrMacSchedulerUeInfoAG");

void NrMacSchedulerUeInfoAG::UpdateDlAGMetric (const NrMacSchedulerNs3::FTResources &totAssigned,
                                               const Ptr<const NrAmc> &amc)
{
  NS_LOG_FUNCTION (this);

  NrMacSchedulerUeInfo::UpdateDlMetric (amc);
  uint32_t tbSize = 0;
  for (const auto &it:m_dlTbSize)
  {
    tbSize += it;
  }
}

void NrMacSchedulerUeInfoAG::UpdateUlAGMetric (const NrMacSchedulerNs3::FTResources &totAssigned,
                                               const Ptr<const NrAmc> &amc)
{
  NS_LOG_FUNCTION (this);

  NrMacSchedulerUeInfo::UpdateUlMetric (amc);

}

// void NrMacSchedulerUeInfoAG::CalculatePotentialTPutDl (const NrMacSchedulerNs3::FTResources &assignableInIteration,
//                                                        const Ptr<const NrAmc> &amc)
// {
//   NS_LOG_FUNCTION (this);
//   uint32_t rbsAssignable = assignableInIteration.m_rbg * GetNumRbPerRbg ();
//   m_potentialTputDl = 0.0;

//   if (this->m_dlCqi.m_ri == 1)
//   {
//     std::vector<uint8_t>::const_iterator mcsIt;
//     mcsIt = std::max_element (m_dlMcs.begin(), m_dlMcs.end());
//     m_potentialTputDl =  amc->CalculateTbSize (*mcsIt, rbsAssignable);
//   }

//   if (this->m_dlCqi.m_ri == 2)
//   {
//     //if the UE supports two streams potential throughput is the sum of
//     //both the TBs.
//     for (const auto &it:m_dlMcs)
//     {
//       m_potentialTputDl +=  amc->CalculateTbSize (it, rbsAssignable);
//     }
//   }

//   m_potentialTputDl /= assignableInIteration.m_sym;
// }

// void NrMacSchedulerUeInfoAG::CalculatePotentialTPutUl (const NrMacSchedulerNs3::FTResources &assignableInIteration,
//                                                        const Ptr<const NrAmc> &amc)
// {
//   NS_LOG_FUNCTION (this);

//   uint32_t rbsAssignable = assignableInIteration.m_rbg * GetNumRbPerRbg ();
//   m_potentialTputUl =  amc->CalculateTbSize (m_ulMcs, rbsAssignable);
//   m_potentialTputUl /= assignableInIteration.m_sym;
// }

} // namespace ns3