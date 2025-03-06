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
#include "nr-mac-scheduler-ofdma-ag.h"
#include "nr-mac-scheduler-ue-info-ag.h"
#include <algorithm>
#include <ns3/double.h>
#include <ns3/log.h>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("NrMacSchedulerOfdmaAG");
NS_OBJECT_ENSURE_REGISTERED (NrMacSchedulerOfdmaAG);

TypeId NrMacSchedulerOfdmaAG::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrMacSchedulerOfdmaAG")
    .SetParent<NrMacSchedulerOfdmaRR> ()
    .AddConstructor<NrMacSchedulerOfdmaAG> ()
  ;
  return tid;
}

NrMacSchedulerOfdmaAG::NrMacSchedulerOfdmaAG () : NrMacSchedulerOfdmaRR ()
{
}

std::shared_ptr<NrMacSchedulerUeInfo>
NrMacSchedulerOfdmaAG::CreateUeRepresentation (const NrMacCschedSapProvider::CschedUeConfigReqParameters &params) const
{
  NS_LOG_FUNCTION (this);
  return std::make_shared <NrMacSchedulerUeInfoAG> (params.m_rnti, params.m_beamConfId,
                                                    std::bind (&NrMacSchedulerOfdmaAG::GetNumRbPerRbg, this));
}

std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                   const NrMacSchedulerNs3::UePtrAndBufferReq &rhs)>
NrMacSchedulerOfdmaAG::GetUeCompareDlFn () const
{
  NS_LOG_FUNCTION (this);
  return NrMacSchedulerUeInfoAG::CompareUeWeightsDl;
}

std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                   const NrMacSchedulerNs3::UePtrAndBufferReq &rhs)>
NrMacSchedulerOfdmaAG::GetUeCompareUlFn () const
{
  NS_LOG_FUNCTION (this);
  
  return NrMacSchedulerUeInfoAG::CompareUeWeightsUl;
}

void NrMacSchedulerOfdmaAG::AssignedDlResources (const UePtrAndBufferReq &ue,
                                                 [[maybe_unused]] const FTResources &assigned,
                                                 const FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  uePtr->UpdateDlAGMetric (totAssigned, m_dlAmc);
}

void NrMacSchedulerOfdmaAG::NotAssignedDlResources (const NrMacSchedulerNs3::UePtrAndBufferReq &ue,
                                                    [[maybe_unused]] const NrMacSchedulerNs3::FTResources &assigned,
                                                    const NrMacSchedulerNs3::FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  uePtr->UpdateDlAGMetric (totAssigned, m_dlAmc);
}

void NrMacSchedulerOfdmaAG::AssignedUlResources (const UePtrAndBufferReq &ue,
                                                 [[maybe_unused]] const FTResources &assigned,
                                                 const FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t age = this->GetAge(ueRnti);
  uePtr->UpdateAge (age);
  uePtr->UpdateUlAGMetric (totAssigned, m_ulAmc);
}

void NrMacSchedulerOfdmaAG::NotAssignedUlResources (const NrMacSchedulerNs3::UePtrAndBufferReq &ue,
                                                    [[maybe_unused]] const NrMacSchedulerNs3::FTResources &assigned,
                                                    const NrMacSchedulerNs3::FTResources &totAssigned) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t age = this->GetAge(ueRnti);
  uePtr->UpdateAge (age);
  uePtr->UpdateUlAGMetric (totAssigned, m_ulAmc);
}

void NrMacSchedulerOfdmaAG::BeforeDlSched (const UePtrAndBufferReq &ue,
                                          const FTResources &assignableInIteration) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  // uePtr->CalculatePotentialTPutDl (assignableInIteration, m_dlAmc);
}

void NrMacSchedulerOfdmaAG::BeforeUlSched (const UePtrAndBufferReq &ue,
                                           const FTResources &assignableInIteration) const
{
  NS_LOG_FUNCTION (this);
  auto uePtr = std::dynamic_pointer_cast<NrMacSchedulerUeInfoAG> (ue.first);
  uint16_t ueRnti = ue.first->GetRnti();
  uint64_t age = this->GetAge(ueRnti);
  uePtr->UpdateAge (age);
  // uePtr->CalculatePotentialTPutUl (assignableInIteration, m_ulAmc);
}

} // namespace ns3