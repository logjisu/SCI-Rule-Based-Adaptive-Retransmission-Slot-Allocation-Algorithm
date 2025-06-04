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
#include "nr-mac-scheduler-ofdma-pg.h"
#include "nr-mac-scheduler-ue-info-pg.h"
#include <algorithm>
#include <ns3/double.h>
#include <ns3/log.h>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("NrMacSchedulerOfdmaPG");
NS_OBJECT_ENSURE_REGISTERED (NrMacSchedulerOfdmaPG);

TypeId NrMacSchedulerOfdmaPG::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrMacSchedulerOfdmaPG")
    .SetParent<NrMacSchedulerOfdmaRR> ()
    .AddConstructor<NrMacSchedulerOfdmaPG> ()
  ;
  return tid;
}

NrMacSchedulerOfdmaPG::NrMacSchedulerOfdmaPG () : NrMacSchedulerOfdmaRR ()
{
}

void
NrMacSchedulerOfdmaPG::SetUePriority (uint16_t rnti, uint32_t priority)
{
  NS_LOG_FUNCTION (this << rnti << priority);
  m_uePriorityMap[rnti] = priority;
}

uint32_t
NrMacSchedulerOfdmaPG::GetUePriority (uint16_t rnti) const
{
  auto it = m_uePriorityMap.find (rnti);
  return (it == m_uePriorityMap.end ()) ? 0 : it->second;
}

std::shared_ptr<NrMacSchedulerUeInfo>
NrMacSchedulerOfdmaPG::CreateUeRepresentation (const NrMacCschedSapProvider::CschedUeConfigReqParameters &params) const
{
  NS_LOG_FUNCTION (this);
  return std::make_shared <NrMacSchedulerUeInfoPG> (params.m_rnti, params.m_beamConfId,
                                                    std::bind (&NrMacSchedulerOfdmaPG::GetNumRbPerRbg, this));
}

std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                   const NrMacSchedulerNs3::UePtrAndBufferReq &rhs)>
NrMacSchedulerOfdmaPG::GetUeCompareDlFn () const
{
  NS_LOG_FUNCTION (this);
  return NrMacSchedulerUeInfoPG::CompareUeWeightsDl;
}

std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                   const NrMacSchedulerNs3::UePtrAndBufferReq &rhs)>
NrMacSchedulerOfdmaPG::GetUeCompareUlFn () const
{
  NS_LOG_FUNCTION (this);
  
  return NrMacSchedulerUeInfoPG::CompareUeWeightsUl;
}

void NrMacSchedulerOfdmaPG::AssignedDlResources (const UePtrAndBufferReq &ue,
                                                 [[maybe_unused]] const FTResources &assigned,
                                                 const FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  uePtr->UpdateDlPGMetric (totAssigned, m_dlAmc);
}

void NrMacSchedulerOfdmaPG::NotAssignedDlResources (const NrMacSchedulerNs3::UePtrAndBufferReq &ue,
                                                    [[maybe_unused]] const NrMacSchedulerNs3::FTResources &assigned,
                                                    const NrMacSchedulerNs3::FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  uePtr->UpdateDlPGMetric (totAssigned, m_dlAmc);
}

void NrMacSchedulerOfdmaPG::AssignedUlResources (const UePtrAndBufferReq &ue,
                                                 [[maybe_unused]] const FTResources &assigned,
                                                 const FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t priority = this->GetUePriority(ueRnti);
  uePtr->UpdatePriority (priority);
  uePtr->UpdateUlPGMetric (totAssigned, m_ulAmc);
}

void NrMacSchedulerOfdmaPG::NotAssignedUlResources (const NrMacSchedulerNs3::UePtrAndBufferReq &ue,
                                                    [[maybe_unused]] const NrMacSchedulerNs3::FTResources &assigned,
                                                    const NrMacSchedulerNs3::FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t priority = this->GetUePriority(ueRnti);
  uePtr->UpdatePriority (priority);
  uePtr->UpdateUlPGMetric (totAssigned, m_ulAmc);
}

void NrMacSchedulerOfdmaPG::BeforeDlSched (const UePtrAndBufferReq &ue,
                                          const FTResources &assignableInIteration) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  // uePtr->CalculatePotentialTPutDl (assignableInIteration, m_dlAmc);
}

void NrMacSchedulerOfdmaPG::BeforeUlSched (const UePtrAndBufferReq &ue,
                                           const FTResources &assignableInIteration) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoPG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t priority = this->GetUePriority(ueRnti);
  uePtr->UpdatePriority (priority);
  // uePtr->CalculatePotentialTPutUl (assignableInIteration, m_ulAmc);
}

} // namespace ns3