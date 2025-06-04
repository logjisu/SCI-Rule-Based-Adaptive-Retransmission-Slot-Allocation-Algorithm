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
#ifndef NR_MAC_SCHEDULER_OFDMA_PG_H
#define NR_MAC_SCHEDULER_OFDMA_PG_H

#include "nr-mac-scheduler-ofdma-rr.h"

namespace ns3 {

class NrMacSchedulerOfdmaPG : public NrMacSchedulerOfdmaRR
{
public:
  /**
   * \brief GetTypeId
   * \return The TypeId of the class
   */
  static TypeId GetTypeId (void);

  /**
   * \brief NrMacSchedulerTdmaPG constructor
   */
  NrMacSchedulerOfdmaPG ();

  /**
   * \brief ~NrMacSchedulerTdmaPG deconstructor
   */
  virtual ~NrMacSchedulerOfdmaPG () override
  {
  }

  /** RNTI에 대한 우선순위 설정 */
  void SetUePriority (uint16_t rnti, uint32_t priority);
  /** RNTI에 설정된 우선순위 반환 (없으면 0) */
  uint32_t GetUePriority (uint16_t rnti) const;

protected:
  virtual std::shared_ptr<NrMacSchedulerUeInfo>
  CreateUeRepresentation (const NrMacCschedSapProvider::CschedUeConfigReqParameters& params) const override;

  virtual std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                             const NrMacSchedulerNs3::UePtrAndBufferReq &rhs )>
  GetUeCompareDlFn () const override;

  virtual std::function<bool(const NrMacSchedulerNs3::UePtrAndBufferReq &lhs,
                             const NrMacSchedulerNs3::UePtrAndBufferReq &rhs )>
  GetUeCompareUlFn () const override;

  virtual void AssignedDlResources (const UePtrAndBufferReq &ue,
                                    const FTResources &assigned,
                                    const FTResources &totAssigned) const override;

  virtual void AssignedUlResources (const UePtrAndBufferReq &ue,
                                    const FTResources &assigned,
                                    const FTResources &totAssigned) const override;

  virtual void NotAssignedDlResources (const UePtrAndBufferReq &ue,
                                       const FTResources &notAssigned,
                                       const FTResources &totalAssigned) const override;

  virtual void NotAssignedUlResources (const UePtrAndBufferReq &ue,
                                       const FTResources &notAssigned,
                                       const FTResources &totalAssigned) const override;

  virtual void BeforeDlSched (const UePtrAndBufferReq &ue,
                              const FTResources &assignableInIteration) const override;

  virtual void BeforeUlSched (const UePtrAndBufferReq &ue,
                              const FTResources &assignableInIteration) const override;

private:
  std::map<uint16_t, uint32_t> m_uePriorityMap; //!< RNTI→Priority
};

} // namespace ns3

#endif /* NR_MAC_SCHEDULER_OFDMA_PG_H */