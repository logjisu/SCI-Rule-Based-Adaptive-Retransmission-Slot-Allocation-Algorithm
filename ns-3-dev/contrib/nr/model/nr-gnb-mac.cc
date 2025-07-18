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

#define NS_LOG_APPEND_CONTEXT                                           \
do{                                                                     \
  std::clog << " [ CellId " << GetCellId() << ", bwpId "                \
               << GetBwpId () << "] ";                                  \
}                                                                       \
while (false);

#include "nr-gnb-mac.h"
#include "nr-phy-mac-common.h"
#include "nr-mac-sched-sap.h"
#include "nr-mac-scheduler.h"
#include "nr-control-messages.h"
#include "nr-mac-pdu-info.h"
#include "nr-mac-header-vs.h"
#include "nr-mac-header-fs-ul.h"
#include "nr-mac-short-bsr-ce.h"

#include <ns3/lte-radio-bearer-tag.h>
#include <ns3/log.h>
#include <ns3/spectrum-model.h>
#include <algorithm>
#include "beam-id.h"

#include "bwp-manager-gnb.h"
#include "a-packet-tags.h"
#include <numeric>  // std::accumulate를 사용하기 위해 필요
#include <queue>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("NrGnbMac");
NS_OBJECT_ENSURE_REGISTERED (NrGnbMac);

// //////////////////////////////////////
// member SAP forwarders
// //////////////////////////////////////

std::unordered_map<uint16_t, uint64_t> m_패킷생성시간;                    // RNTI별 패킷 생성 시간 저장(RNTI, 패킷 생성 시간)
std::unordered_map<uint16_t, uint64_t> m_패킷받은시간;                    // RNTI별 패킷 받은 시간 저장(RNTI, 패킷 받은 시간)
std::unordered_map<uint16_t, uint16_t> m_패킷우선순위;                    // RNTI별 패킷 우선순위 저장(RNTI, 패킷 우선순위)
std::unordered_map<uint16_t, std::queue<NrGnbMac::AgeEntry>> m_AgeQueues; // RNTI별 받은 시간과 Age 쌍을 순서대로 보관하는 큐(RNTI, 우선순위위, Age, count)
std::vector<uint64_t> m_AoI;                                              // 스케줄링된 Age 값을 저장하는 벡터
std::map<uint16_t, uint64_t> rntiToTotalBytes;                            // RNTI 별 총 패킷 크기를 저장하는 맵
static std::map<uint32_t,uint16_t> ueIndexToRnti;
static std::map<uint16_t,uint32_t> rntiToUeIndex;

class NrGnbMacMemberEnbCmacSapProvider : public LteEnbCmacSapProvider{
public:
  NrGnbMacMemberEnbCmacSapProvider (NrGnbMac* mac);

  // inherited from LteEnbCmacSapProvider
  virtual void ConfigureMac (uint16_t ulBandwidth, uint16_t dlBandwidth);
  virtual void AddUe (uint16_t rnti);
  virtual void RemoveUe (uint16_t rnti);
  virtual void AddLc (LcInfo lcinfo, LteMacSapUser* msu);
  virtual void ReconfigureLc (LcInfo lcinfo);
  virtual void ReleaseLc (uint16_t rnti, uint8_t lcid);
  virtual void UeUpdateConfigurationReq (UeConfig params);
  virtual RachConfig GetRachConfig ();
  virtual AllocateNcRaPreambleReturnValue AllocateNcRaPreamble (uint16_t rnti);
private:
 NrGnbMac* m_mac;
};

NrGnbMacMemberEnbCmacSapProvider::NrGnbMacMemberEnbCmacSapProvider (NrGnbMac* mac)
 : m_mac (mac){
  
}
// 시뮬레이션 시작 시 PeriodicAgeUpdate()를 시작하는 함수
void NrGnbMac::StartPeriodicAgeUpdate(uint16_t TTI, uint32_t deadline){
  NS_LOG_INFO("Starting periodic Age update every 100µs");
  PeriodicAgeUpdate(TTI, deadline);
}

// 주기적 Age 증가 로직 (100µs마다 호출)
void NrGnbMac::IncrementAgeOnNoPacket(uint16_t rnti, uint32_t deadline) {
  // 패킷을 받지 않은 UE만 Age 증가
  if (m_패킷받은시간.find(rnti) == m_패킷받은시간.end()) {
    auto& ageQueue = m_AgeQueues[rnti];

    if (!ageQueue.empty()) {
      AgeEntry& 안받은 = ageQueue.back();
      안받은.age++;

      // deadline 넘으면, Age를 1로 초기화
      if (안받은.age >= deadline) {
        안받은.age = 1;
      }
    }
  }
  else {
    // 패킷을 받은 UE는 m_AgeQueue에 저장된 우선순위를 기반하여 계산
    auto& ageQueue = m_AgeQueues[rnti];
    if (!ageQueue.empty()) {
      // 저장된 우선순위 가져오기
      AgeEntry& 받은 = ageQueue.back();
      uint32_t w = 받은.priority * 받은.count;
      
      받은.age += w;
      받은.count += 1;
      
      // deadline 넘으면, Age를 1로 초기화
      if (받은.age >= deadline) {
        받은.age   = 1;
        받은.count = 1;
      }
      NS_LOG_INFO("UE : " << rnti << "\t Age : " << 받은.age << "\t AgeUpdate/100µs");
    }
  }
}

// 패킷 처리 시 Age 초기화 및 패킷 수신 여부 초기화
void
NrGnbMac::ResetAgeOnPacketProcessed(uint16_t rnti) {
  if (m_AgeQueues.find(rnti) != m_AgeQueues.end()) {
    auto& ageQueue = m_AgeQueues[rnti];
    while (!ageQueue.empty()) {
      ageQueue.pop(); // 처리 완료 후 제거
    }
  }
  m_패킷받은시간.erase(rnti); // 수신 정보 초기화
}

// 1ms마다 호출될 주기적 Age 관리 함수
void
NrGnbMac::PeriodicAgeUpdate(uint16_t updateTTI, uint32_t deadline) {
  NS_LOG_FUNCTION(this);
  for (auto& entry : m_AgeQueues) {
    IncrementAgeOnNoPacket(entry.first, deadline);  // 예시: Deadline 100ms
  }
  // 1µs 이후 다시 호출 (반복 스케줄링)
  Simulator::Schedule(MicroSeconds(updateTTI), &NrGnbMac::PeriodicAgeUpdate, this, updateTTI, deadline);
}

void
NrGnbMac::ActivateUe(uint16_t rnti){
  m_activeUes.insert(rnti);
}

void
NrGnbMac::DeactivateUe(uint16_t rnti){
  m_activeUes.erase(rnti);
}

void
NrGnbMacMemberEnbCmacSapProvider::ConfigureMac (uint16_t ulBandwidth, uint16_t dlBandwidth){
  m_mac->DoConfigureMac (ulBandwidth, dlBandwidth);
}

void
NrGnbMacMemberEnbCmacSapProvider::AddUe (uint16_t rnti){
  m_mac->DoAddUe (rnti);
}

void
NrGnbMacMemberEnbCmacSapProvider::RemoveUe (uint16_t rnti){
  m_mac->DoRemoveUe (rnti);
}

void
NrGnbMacMemberEnbCmacSapProvider::AddLc (LcInfo lcinfo, LteMacSapUser* msu){
  m_mac->DoAddLc (lcinfo, msu);
}

void
NrGnbMacMemberEnbCmacSapProvider::ReconfigureLc (LcInfo lcinfo){
  m_mac->DoReconfigureLc (lcinfo);
}

void
NrGnbMacMemberEnbCmacSapProvider::ReleaseLc (uint16_t rnti, uint8_t lcid){
  m_mac->DoReleaseLc (rnti, lcid);
}

void
NrGnbMacMemberEnbCmacSapProvider::UeUpdateConfigurationReq (UeConfig params){
  m_mac->UeUpdateConfigurationReq (params);
}

LteEnbCmacSapProvider::RachConfig
NrGnbMacMemberEnbCmacSapProvider::GetRachConfig (){
  return m_mac->DoGetRachConfig ();
}

LteEnbCmacSapProvider::AllocateNcRaPreambleReturnValue
NrGnbMacMemberEnbCmacSapProvider::AllocateNcRaPreamble (uint16_t rnti){
  return m_mac->DoAllocateNcRaPreamble (rnti);
}



// SAP interface between ENB PHY AND MAC
// PHY is provider and MAC is user of its service following OSI model.
// However, PHY may request some information from MAC.
class NrMacEnbMemberPhySapUser : public NrGnbPhySapUser{
public:
  NrMacEnbMemberPhySapUser (NrGnbMac* mac);

  virtual void ReceivePhyPdu (Ptr<Packet> p) override;
  virtual void ReceiveControlMessage (Ptr<NrControlMessage> msg) override;

  virtual void SlotDlIndication (const SfnSf &, LteNrTddSlotType) override;
  virtual void SlotUlIndication (const SfnSf &, LteNrTddSlotType) override;

  virtual void SetCurrentSfn (const SfnSf &) override;
  virtual void UlCqiReport (NrMacSchedSapProvider::SchedUlCqiInfoReqParameters cqi) override;
  virtual void ReceiveRachPreamble (uint32_t raId) override;
  virtual void UlHarqFeedback (UlHarqInfo params) override;
  virtual void BeamChangeReport (BeamConfId beamConfId, uint8_t rnti) override;

  virtual uint32_t GetNumRbPerRbg () const override;

  virtual std::shared_ptr<DciInfoElementTdma> GetDlCtrlDci () const override;
  virtual std::shared_ptr<DciInfoElementTdma> GetUlCtrlDci () const override;
private:
  NrGnbMac* m_mac;
};

NrMacEnbMemberPhySapUser::NrMacEnbMemberPhySapUser (NrGnbMac* mac)
 : m_mac (mac){

}

void
NrMacEnbMemberPhySapUser::ReceivePhyPdu (Ptr<Packet> p){
  m_mac->DoReceivePhyPdu (p);
}

void
NrMacEnbMemberPhySapUser::ReceiveControlMessage (Ptr<NrControlMessage> msg){
  m_mac->DoReceiveControlMessage (msg);
}

void
NrMacEnbMemberPhySapUser::SlotDlIndication (const SfnSf &sfn, LteNrTddSlotType type){
  m_mac->DoSlotDlIndication (sfn, type);
}

void
NrMacEnbMemberPhySapUser::SlotUlIndication (const SfnSf &sfn, LteNrTddSlotType type){
  m_mac->DoSlotUlIndication (sfn, type);
}

void
NrMacEnbMemberPhySapUser::SetCurrentSfn (const SfnSf &sfn){
  m_mac->SetCurrentSfn (sfn);
}

void
NrMacEnbMemberPhySapUser::UlCqiReport (NrMacSchedSapProvider::SchedUlCqiInfoReqParameters ulcqi){
  m_mac->DoUlCqiReport (ulcqi);
}

void
NrMacEnbMemberPhySapUser::ReceiveRachPreamble (uint32_t raId){
  m_mac->ReceiveRachPreamble (raId);
}

void
NrMacEnbMemberPhySapUser::UlHarqFeedback (UlHarqInfo params){
  m_mac->DoUlHarqFeedback (params);
}

void
NrMacEnbMemberPhySapUser::BeamChangeReport (BeamConfId beamConfId, uint8_t rnti){
  m_mac->BeamChangeReport (beamConfId, rnti);
}

uint32_t
NrMacEnbMemberPhySapUser::GetNumRbPerRbg () const{
  return m_mac->GetNumRbPerRbg();
}

std::shared_ptr<DciInfoElementTdma>
NrMacEnbMemberPhySapUser::GetDlCtrlDci() const{
  return m_mac->GetDlCtrlDci ();
}

std::shared_ptr<DciInfoElementTdma>
NrMacEnbMemberPhySapUser::GetUlCtrlDci() const{
  return m_mac->GetUlCtrlDci ();
}

// MAC Sched

class NrMacMemberMacSchedSapUser : public NrMacSchedSapUser{
public:
  NrMacMemberMacSchedSapUser (NrGnbMac* mac);
  virtual void SchedConfigInd (const struct SchedConfigIndParameters& params) override;
  virtual Ptr<const SpectrumModel> GetSpectrumModel () const override;
  virtual uint32_t GetNumRbPerRbg () const override;
  virtual uint8_t GetNumHarqProcess () const override;
  virtual uint16_t GetBwpId () const override;
  virtual uint16_t GetCellId () const override;
  virtual uint32_t GetSymbolsPerSlot () const override;
  virtual Time GetSlotPeriod () const override;
  // Configured Grant
  virtual Time GetTbUlEncodeLatency () const override;
private:
  NrGnbMac* m_mac;
};

NrMacMemberMacSchedSapUser::NrMacMemberMacSchedSapUser (NrGnbMac* mac)
 : m_mac (mac)
{
 //  Some blank spaces
}

void
NrMacMemberMacSchedSapUser::SchedConfigInd (const struct SchedConfigIndParameters& params){
  m_mac->DoSchedConfigIndication (params);
}

Ptr<const SpectrumModel>
NrMacMemberMacSchedSapUser::GetSpectrumModel () const{
  return m_mac->m_phySapProvider->GetSpectrumModel (); //  MAC forwards the call from scheduler to PHY; i.e. this function connects two providers of MAC: scheduler and PHY
}

uint32_t
NrMacMemberMacSchedSapUser::GetNumRbPerRbg () const{
  return m_mac->GetNumRbPerRbg ();
}

uint8_t
NrMacMemberMacSchedSapUser::GetNumHarqProcess () const{
  return m_mac->GetNumHarqProcess();
}

uint16_t
NrMacMemberMacSchedSapUser::GetBwpId() const{
  return m_mac->GetBwpId ();
}

uint16_t
NrMacMemberMacSchedSapUser::GetCellId() const{
  return m_mac->GetCellId ();
}

uint32_t
NrMacMemberMacSchedSapUser::GetSymbolsPerSlot() const{
  return m_mac->m_phySapProvider->GetSymbolsPerSlot ();
}

Time
NrMacMemberMacSchedSapUser::GetSlotPeriod() const{
  return m_mac->m_phySapProvider->GetSlotPeriod ();
}

// Configured Grant
Time
NrMacMemberMacSchedSapUser::GetTbUlEncodeLatency() const
{
  return m_mac->m_phySapProvider->GetTbUlEncodeLatency ();
}

class NrMacMemberMacCschedSapUser : public NrMacCschedSapUser{
public:
  NrMacMemberMacCschedSapUser (NrGnbMac* mac);

  virtual void CschedCellConfigCnf (const struct NrMacCschedSapUser::CschedCellConfigCnfParameters& params);
  virtual void CschedUeConfigCnf (const struct NrMacCschedSapUser::CschedUeConfigCnfParameters& params);
  virtual void CschedLcConfigCnf (const struct NrMacCschedSapUser::CschedLcConfigCnfParameters& params);
  virtual void CschedLcReleaseCnf (const struct NrMacCschedSapUser::CschedLcReleaseCnfParameters& params);
  virtual void CschedUeReleaseCnf (const struct NrMacCschedSapUser::CschedUeReleaseCnfParameters& params);
  virtual void CschedUeConfigUpdateInd (const struct NrMacCschedSapUser::CschedUeConfigUpdateIndParameters& params);
  virtual void CschedCellConfigUpdateInd (const struct NrMacCschedSapUser::CschedCellConfigUpdateIndParameters& params);

private:
  NrGnbMac* m_mac;
};


NrMacMemberMacCschedSapUser::NrMacMemberMacCschedSapUser (NrGnbMac* mac)
 : m_mac (mac)
{
}

void
NrMacMemberMacCschedSapUser::CschedCellConfigCnf (const struct CschedCellConfigCnfParameters& params){
  m_mac->DoCschedCellConfigCnf (params);
}

void
NrMacMemberMacCschedSapUser::CschedUeConfigCnf (const struct CschedUeConfigCnfParameters& params){
  m_mac->DoCschedUeConfigCnf (params);
}

void
NrMacMemberMacCschedSapUser::CschedLcConfigCnf (const struct CschedLcConfigCnfParameters& params){
  m_mac->DoCschedLcConfigCnf (params);
}

void
NrMacMemberMacCschedSapUser::CschedLcReleaseCnf (const struct CschedLcReleaseCnfParameters& params){
  m_mac->DoCschedLcReleaseCnf (params);
}

void
NrMacMemberMacCschedSapUser::CschedUeReleaseCnf (const struct CschedUeReleaseCnfParameters& params){
  m_mac->DoCschedUeReleaseCnf (params);
}

void
NrMacMemberMacCschedSapUser::CschedUeConfigUpdateInd (const struct CschedUeConfigUpdateIndParameters& params){
  m_mac->DoCschedUeConfigUpdateInd (params);
}

void
NrMacMemberMacCschedSapUser::CschedCellConfigUpdateInd (const struct CschedCellConfigUpdateIndParameters& params){
  m_mac->DoCschedCellConfigUpdateInd (params);
}

TypeId
NrGnbMac::GetTypeId (void){
  static TypeId tid = TypeId ("ns3::NrGnbMac")
    .SetParent<Object> ()
    .AddConstructor<NrGnbMac> ()
    .AddAttribute ("NumRbPerRbg", "Number of resource blocks per resource block group.",
                    UintegerValue (1),
                    MakeUintegerAccessor (&NrGnbMac::SetNumRbPerRbg, &NrGnbMac::GetNumRbPerRbg),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NumHarqProcess", "Number of concurrent stop-and-wait Hybrid ARQ processes per user",
                    UintegerValue (20),
                    MakeUintegerAccessor (&NrGnbMac::SetNumHarqProcess, &NrGnbMac::GetNumHarqProcess),
                    MakeUintegerChecker<uint8_t> ())
    .AddTraceSource ("DlScheduling", "Information regarding DL scheduling.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_dlScheduling),
                      "ns3::NrGnbMac::DlSchedulingTracedCallback")
    .AddTraceSource ("UlScheduling", "Information regarding UL scheduling.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_ulScheduling),
                      "ns3::NrGnbMac::SchedulingTracedCallback")
    .AddTraceSource ("SrReq", "Information regarding received scheduling request.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_srCallback),
                      "ns3::NrGnbMac::SrTracedCallback")
    .AddTraceSource ("GnbMacRxedCtrlMsgsTrace", "Enb MAC Rxed Control Messages Traces.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_macRxedCtrlMsgsTrace),
                      "ns3::NrMacRxTrace::RxedGnbMacCtrlMsgsTracedCallback")
    .AddTraceSource ("GnbMacTxedCtrlMsgsTrace", "Enb MAC Txed Control Messages Traces.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_macTxedCtrlMsgsTrace),
                      "ns3::NrMacRxTrace::TxedGnbMacCtrlMsgsTracedCallback")
    .AddTraceSource ("DlHarqFeedback", "Harq feedback.",
                      MakeTraceSourceAccessor (&NrGnbMac::m_dlHarqFeedback),
                      "ns3::NrGnbMac::DlHarqFeedbackTracedCallback")
      // Configured Grant
    .AddAttribute ("CG", "Activate configured grant scheduling for UL periodic transmissions",
                  BooleanValue (true),
                  MakeBooleanAccessor (&NrGnbMac::SetCG, &NrGnbMac::GetCG),
                  MakeBooleanChecker ())
    .AddAttribute ("ConfigurationTime", "Time required to configure UE with configured grant signal",
                    UintegerValue (10),
                    MakeUintegerAccessor (&NrGnbMac::SetConfigurationTime, &NrGnbMac::GetConfigurationTime),
                    MakeUintegerChecker<uint8_t> ())
  ;
  return tid;
}

NrGnbMac::NrGnbMac (void) : Object (){
  NS_LOG_FUNCTION (this);
  m_cmacSapProvider = new NrGnbMacMemberEnbCmacSapProvider (this);
  m_macSapProvider = new EnbMacMemberLteMacSapProvider<NrGnbMac> (this);
  m_phySapUser = new NrMacEnbMemberPhySapUser (this);
  m_macSchedSapUser = new NrMacMemberMacSchedSapUser (this);
  m_macCschedSapUser = new NrMacMemberMacCschedSapUser (this);
  m_ccmMacSapProvider = new MemberLteCcmMacSapProvider<NrGnbMac> (this);
}

NrGnbMac::~NrGnbMac (void){
  NS_LOG_FUNCTION (this);
}

void
NrGnbMac::DoDispose (){
  NS_LOG_FUNCTION (this);
  m_dlCqiReceived.clear ();
  m_ulCqiReceived.clear ();
  m_ulCeReceived.clear ();
  m_miDlHarqProcessesPackets.clear ();
  delete m_macSapProvider;
  delete m_cmacSapProvider;
  delete m_macSchedSapUser;
  delete m_macCschedSapUser;
  delete m_phySapUser;
  delete m_ccmMacSapProvider;
}

void
NrGnbMac::SetNumRbPerRbg (uint32_t rbgSize){
  NS_ABORT_MSG_IF (m_numRbPerRbg !=-1, "This attribute can not be reconfigured");
  m_numRbPerRbg = rbgSize;
}

uint32_t
NrGnbMac::GetNumRbPerRbg (void) const{
  return m_numRbPerRbg;
}

void
NrGnbMac::SetNumHarqProcess (uint8_t numHarqProcess){
  m_numHarqProcess = numHarqProcess;
}

/**
* \return number of HARQ processes
*/
uint8_t
NrGnbMac::GetNumHarqProcess () const{
  return m_numHarqProcess;
}

uint8_t
NrGnbMac::GetDlCtrlSyms() const{
  return m_macSchedSapProvider->GetDlCtrlSyms ();
}

uint8_t
NrGnbMac::GetUlCtrlSyms() const{
  return m_macSchedSapProvider->GetUlCtrlSyms ();
}

void
NrGnbMac::ReceiveRachPreamble (uint32_t raId){
  Ptr<NrRachPreambleMessage> rachMsg = Create<NrRachPreambleMessage> ();
  rachMsg->SetSourceBwp (GetBwpId ());
  m_macRxedCtrlMsgsTrace (m_currentSlot, GetCellId (), raId, GetBwpId (), rachMsg);

  ++m_receivedRachPreambleCount[raId];
}

LteMacSapProvider*
NrGnbMac::GetMacSapProvider (void){
  return m_macSapProvider;
}

LteEnbCmacSapProvider*
NrGnbMac::GetEnbCmacSapProvider (void){
  return m_cmacSapProvider;
}
void
NrGnbMac::SetEnbCmacSapUser (LteEnbCmacSapUser* s){
  m_cmacSapUser = s;
}

void
NrGnbMac::SetLteCcmMacSapUser (LteCcmMacSapUser* s){
  m_ccmMacSapUser = s;
}


LteCcmMacSapProvider*
NrGnbMac::GetLteCcmMacSapProvider (){
  NS_LOG_FUNCTION (this);
  return m_ccmMacSapProvider;
}

void
NrGnbMac::SetCurrentSfn (const SfnSf &sfnSf){
  NS_LOG_FUNCTION (this);
  m_currentSlot = sfnSf;
}

void
NrGnbMac::DoSlotDlIndication (const SfnSf &sfnSf, LteNrTddSlotType type){
  NS_LOG_FUNCTION (this);
  //NS_LOG_INFO ("Perform things on DL, slot on the air: " << sfnSf);

  // --- DOWNLINK ---
  // Send Dl-CQI info to the scheduler    if(m_dlCqiReceived.size () > 0)
  {
    NrMacSchedSapProvider::SchedDlCqiInfoReqParameters dlCqiInfoReq;
    dlCqiInfoReq.m_sfnsf = sfnSf;

    dlCqiInfoReq.m_cqiList.insert (dlCqiInfoReq.m_cqiList.begin (), m_dlCqiReceived.begin (), m_dlCqiReceived.end ());
    m_dlCqiReceived.erase (m_dlCqiReceived.begin (), m_dlCqiReceived.end ());

    m_macSchedSapProvider->SchedDlCqiInfoReq (dlCqiInfoReq);

    for (const auto & v : dlCqiInfoReq.m_cqiList)
      {
        Ptr<NrDlCqiMessage> msg = Create<NrDlCqiMessage> ();
        msg->SetDlCqi (v);
        m_macRxedCtrlMsgsTrace (m_currentSlot, GetCellId (), v.m_rnti, GetBwpId (), msg);
      }
  }

  if (!m_receivedRachPreambleCount.empty ())
    {
      // process received RACH preambles and notify the scheduler
      NrMacSchedSapProvider::SchedDlRachInfoReqParameters rachInfoReqParams;

      for (auto it = m_receivedRachPreambleCount.begin ();
            it != m_receivedRachPreambleCount.end ();
            ++it)
        {
          uint16_t rnti = m_cmacSapUser->AllocateTemporaryCellRnti ();

          NS_LOG_INFO ("Informing MAC scheduler of the RACH preamble for " <<
                        static_cast<uint16_t> (it->first) << " in slot " << sfnSf);
          RachListElement_s rachLe;
          rachLe.m_rnti = rnti;
          rachLe.m_estimatedSize = 144; // to be confirmed
          rachInfoReqParams.m_rachList.emplace_back (rachLe);

          m_rapIdRntiMap.insert (std::make_pair (rnti, it->first));
        }
      m_receivedRachPreambleCount.clear ();
      m_macSchedSapProvider->SchedDlRachInfoReq (rachInfoReqParams);
    }

  NrMacSchedSapProvider::SchedDlTriggerReqParameters dlParams;

  dlParams.m_slotType = type;
  dlParams.m_snfSf = sfnSf;

  // Forward DL HARQ feedbacks collected during last subframe TTI
  if (m_dlHarqInfoReceived.size () > 0)
    {
      dlParams.m_dlHarqInfoList = m_dlHarqInfoReceived;
      // empty local buffer
      m_dlHarqInfoReceived.clear ();

      for (const auto & v : dlParams.m_dlHarqInfoList)
      {
        Ptr<NrDlHarqFeedbackMessage> msg = Create <NrDlHarqFeedbackMessage> ();
        msg->SetDlHarqFeedback (v);
        m_macRxedCtrlMsgsTrace (m_currentSlot, GetCellId (), v.m_rnti, GetBwpId (), msg);
      }
    }

  {
    for (const auto & ue : m_rlcAttached)
    {
      NrMacCschedSapProvider::CschedUeConfigReqParameters params;
      params.m_rnti = ue.first;
      params.m_beamConfId = m_phySapProvider->GetBeamConfId (ue.first);
      params.m_transmissionMode = 0;   // set to default value (SISO) for avoiding random initialization (valgrind error)
      m_macCschedSapProvider->CschedUeConfigReq (params);
    }
  }

  m_macSchedSapProvider->SchedDlTriggerReq (dlParams);
}

/*
 * 
 */
void
NrGnbMac::DoSlotUlIndication (const SfnSf &sfnSf, LteNrTddSlotType type){
  NS_LOG_FUNCTION (this);
  //NS_LOG_INFO ("Perform things on UL, slot on the air: " << sfnSf);

  // --- 상향링크(UPLINK) ---
  // 스케줄러에 UL-CQI 정보 전송
  for (uint16_t i = 0; i < m_ulCqiReceived.size (); i++){
    //m_ulCqiReceived.at (i).m_sfnSf = ((0x3FF & frameNum) << 16) | ((0xFF & subframeNum) << 8) | (0xFF & varTtiNum);
    m_macSchedSapProvider->SchedUlCqiInfoReq (m_ulCqiReceived.at (i));
  }
  m_ulCqiReceived.clear ();
  if (m_cgScheduling){
    static SfnSf m_cgr_configuration = SfnSf (0, 0, 0, sfnSf.GetNumerology ());
    uint8_t posCG = 0;
    uint8_t numberOfSlot_insideOneSubframe = pow(2,(sfnSf.GetNumerology ())); // μ -> 한 subframe 당 슬롯 개수
    if (m_currentSlot < m_cgr_configuration) {
      for (auto rnti : m_srRntiList) {
        uint8_t number_slots_for_processing_configurationPeriod = 7;
        uint8_t number_slots_configuration = (m_configurationTime * numberOfSlot_insideOneSubframe) - number_slots_for_processing_configurationPeriod;
        if(rnti != 0){
          if (!m_cgrConfigured[rnti]) {
            m_cgrConfigured[rnti] = true;
            m_cgr_configuration = m_currentSlot;
            m_cgr_configuration.Add (number_slots_configuration);
            paramsCG_rntiSlot[posCG].m_snfSf = m_cgr_configuration;
          }
          else {
            posCG++;
            m_cgrNextTxSlot = m_currentSlot;
            m_cgrNextTxSlot.Add(number_slots_configuration);
            paramsCG_rntiSlot[m_posCgrIndex[rnti]].m_snfSf = m_cgrNextTxSlot;
          }
          // 해당 posCG 슬롯에 Sr/Buf/Traffic 등 CGR 파라미터 저장
          paramsCG_rntiSlot[posCG].m_srList.insert (paramsCG_rntiSlot[posCG].m_srList.begin(), m_srRntiList.begin (), m_srRntiList.end ());
          paramsCG_rntiSlot[posCG].m_bufCgr.insert (paramsCG_rntiSlot[posCG].m_bufCgr.begin(), m_cgrBufSizeList.begin (), m_cgrBufSizeList.end ());
          paramsCG_rntiSlot[posCG].m_TraffPCgr.insert (paramsCG_rntiSlot[posCG].m_TraffPCgr.begin(), m_cgrTraffP.begin (), m_cgrTraffP.end ());
          paramsCG_rntiSlot[posCG].lcid = lcid_configuredGrant;
          paramsCG_rntiSlot[posCG].m_TraffInitCgr.insert (paramsCG_rntiSlot[posCG].m_TraffInitCgr.begin(), m_cgrTraffInit.begin (), m_cgrTraffInit.end ());
          paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.insert (paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.begin(), m_cgrTraffDeadline.begin (), m_cgrTraffDeadline.end ());
          // 최대 posCG 개수 저장
          countCG_slots = posCG;
          // 한 번만 구성하고 빠져나감
          break;
          }
      }
    }
    else {
      posCG = 0;
      while (posCG <= countCG_slots) {
        if (paramsCG_rntiSlot[posCG].m_snfSf == m_currentSlot) { // 예약해 둔 슬롯과 지금 슬롯이 딱 일치하면
          auto rntiIt = paramsCG_rntiSlot[posCG].m_srList.begin ();
          auto bufIt = paramsCG_rntiSlot[posCG].m_bufCgr.begin ();
          auto traffPIt = paramsCG_rntiSlot[posCG].m_TraffPCgr.begin ();
          auto traffInitIt = paramsCG_rntiSlot[posCG].m_TraffInitCgr.begin ();
          auto traffDeadlineIt = paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.begin ();
        
          while (rntiIt != paramsCG_rntiSlot[posCG].m_srList.end ()) {
            m_ccmMacSapUser->UlReceiveCgr (*rntiIt, componentCarrierId_configuredGrant, *bufIt,lcid_configuredGrant,*traffPIt, *traffInitIt, *traffDeadlineIt);
            m_cgrNextTxSlot = m_currentSlot;
            uint8_t number_slots_configurateGrantPeriod = *traffPIt*numberOfSlot_insideOneSubframe;
            m_cgrNextTxSlot.Add(number_slots_configurateGrantPeriod);
            paramsCG_rntiSlot[posCG].m_snfSf = m_cgrNextTxSlot;
            rntiIt++;
            bufIt++;
            traffPIt++;
          }
          posCG = 0;
          break;
        }
        posCG ++;
      }
    }
    // static bool cgr_configuration = false; // CG 설정 상태를 유지하기 위한 static 변수
    // static SfnSf m_cgr_configuration = SfnSf (0, 0, 0, sfnSf.GetNumerology ()); // 최초 CG 구성 시점을 저장할 변수 (Numerology에 맞추어 초기화)
    // static uint8_t posCG = 0; // CG 구성 슬롯 후보를 인덱스별로 저장하는 배열 인덱스
    // uint8_t numberOfSlot_insideOneSubframe = pow(2,(sfnSf.GetNumerology ())); // 한 subframe(1 ms) 안에 몇 개의 슬롯이 있는지 계산 (μ=Numerology)
    // if (!cgr_configuration || m_currentSlot < m_cgr_configuration) {
    //   for (const auto & v : m_srRntiList) {
    //     uint8_t number_slots_for_processing_configurationPeriod = 7;
    //     uint8_t number_slots_configuration = (m_configurationTime*numberOfSlot_insideOneSubframe)-number_slots_for_processing_configurationPeriod;
    //     if(v != 0 ) {
    //       if (!cgr_configuration) {
    //         // 우리는 미리 할당된 자원만을 사용하여 전송이 얼마나 많은 슬롯에서 이루어질지 계산합니다
    //         cgr_configuration = true;
    //         m_cgr_configuration = m_currentSlot;
    //         m_cgr_configuration.Add(number_slots_configuration);
    //         paramsCG_rntiSlot[posCG].m_snfSf = m_cgr_configuration;
    //       }
    //       else {
    //         // 이후 CG 추가 구성 (POSCG 증가)
    //         posCG ++;
    //         m_cgrNextTxSlot = m_currentSlot;
    //         m_cgrNextTxSlot.Add(number_slots_configuration);
    //         paramsCG_rntiSlot[posCG].m_snfSf = m_cgrNextTxSlot;
    //       }
          
    //       // 해당 posCG 슬롯에 Sr/Buf/Traffic 등 CGR 파라미터 저장
    //       paramsCG_rntiSlot[posCG].m_srList.insert (paramsCG_rntiSlot[posCG].m_srList.begin(), m_srRntiList.begin (), m_srRntiList.end ());
    //       paramsCG_rntiSlot[posCG].m_bufCgr.insert (paramsCG_rntiSlot[posCG].m_bufCgr.begin(), m_cgrBufSizeList.begin (), m_cgrBufSizeList.end ());
    //       paramsCG_rntiSlot[posCG].m_TraffPCgr.insert (paramsCG_rntiSlot[posCG].m_TraffPCgr.begin(), m_cgrTraffP.begin (), m_cgrTraffP.end ());
    //       paramsCG_rntiSlot[posCG].lcid = lcid_configuredGrant;
    //       paramsCG_rntiSlot[posCG].m_TraffInitCgr.insert (paramsCG_rntiSlot[posCG].m_TraffInitCgr.begin(), m_cgrTraffInit.begin (), m_cgrTraffInit.end ());
    //       paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.insert (paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.begin(), m_cgrTraffDeadline.begin (), m_cgrTraffDeadline.end ());
          
    //       // 최대 posCG 개수 저장
    //       countCG_slots = posCG;
    //       // 한 번만 구성하고 빠져나감
    //       break;
    //     }
    //   }
    // }
    // else { // “이미 구성 완료” 이고 “지금 슬롯이 구성 시점을 지났다면” → “전송 단계”
    //   posCG = 0;
    //   // 미리 계산해둔 각 CG 전송 시점 슬롯들과 비교
    //   while (posCG <= countCG_slots){
    //     if (paramsCG_rntiSlot[posCG].m_snfSf == m_currentSlot) { // 예약해 둔 슬롯과 지금 슬롯이 딱 일치하면
    //       // 그 posCG 에 저장된 UE 리스트만큼 반복
    //       auto rntiIt = paramsCG_rntiSlot[posCG].m_srList.begin ();
    //       auto bufIt = paramsCG_rntiSlot[posCG].m_bufCgr.begin ();
    //       auto traffPIt = paramsCG_rntiSlot[posCG].m_TraffPCgr.begin ();
    //       auto traffInitIt = paramsCG_rntiSlot[posCG].m_TraffInitCgr.begin ();
    //       auto traffDeadlineIt = paramsCG_rntiSlot[posCG].m_TraffDeadlineCgr.begin ();
    //       // 모든 UE에 대해 UlReceiverCgr() 호출 -> 실제 UL grant 전달
    //       while (rntiIt != paramsCG_rntiSlot[posCG].m_srList.end ()){
    //         // 실제 gNB→UE 로 CGR 메시지 전송
    //         m_ccmMacSapUser->UlReceiveCgr (*rntiIt, componentCarrierId_configuredGrant, *bufIt,lcid_configuredGrant,*traffPIt, *traffInitIt, *traffDeadlineIt);
    //         m_cgrNextTxSlot = m_currentSlot; // “다음 전송 시점” = 지금 + traffP * slotsPerSubframe
    //         uint8_t number_slots_configurateGrantPeriod = *traffPIt*numberOfSlot_insideOneSubframe;
    //         m_cgrNextTxSlot.Add(number_slots_configurateGrantPeriod);
    //         paramsCG_rntiSlot[posCG].m_snfSf = m_cgrNextTxSlot; // 예약 슬롯 업데이트
    //         rntiIt++;
    //         bufIt++;
    //         traffPIt++;
    //       }
    //       posCG = 0; // 다시 처음부터 검사
    //       break;
    //     }
    //     posCG ++;
    //   }
    // }
    { // 스케줄러에 CGR 정보 요청
      NrMacSchedSapProvider::SchedUlCgrInfoReqParameters params;
      params.m_snfSf = m_currentSlot;
      params.m_srList.insert (params.m_srList.begin(), m_srRntiList.begin (), m_srRntiList.end ());
      params.m_bufCgr.insert(params.m_bufCgr.begin(), m_cgrBufSizeList.begin (), m_cgrBufSizeList.end ());
      params.lcid = lcid_configuredGrant;
      params.m_TraffPCgr.insert (params.m_TraffPCgr.begin(), m_cgrTraffP.begin (), m_cgrTraffP.end ());
      params.m_TraffInitCgr.insert (params.m_TraffInitCgr.begin(), m_cgrTraffInit.begin (), m_cgrTraffInit.end ());
      params.m_TraffDeadlineCgr.insert (params.m_TraffDeadlineCgr.begin(), m_cgrTraffDeadline.begin (), m_cgrTraffDeadline.end ());
      
      for (const auto & v : m_srRntiList){
        auto& ageQueue = m_AgeQueues[v];
        if (!ageQueue.empty()){
          const AgeEntry &e = ageQueue.front();
          params.m_ageList.push_back(e.age);
        }
        else{
          params.m_ageList.push_back(1); // Age 정보가 없으면 1로 설정
        }
      }

      // 일괄 전송 후 리스트 초기화
      m_srRntiList.clear();
      m_cgrBufSizeList.clear();
      m_cgrTraffP.clear();
      m_cgrTraffDeadline.clear();
      m_cgrTraffInit.clear();

      m_macSchedSapProvider->SchedUlCgrInfoReq (params);
    }

    // We do not have to send UL BSR reports to the scheduler because it uses configured grant,
    // all the resources are allocated.
    // UL BSR/Ce 보고는 CG 모드에서는 불필요하므로 클리어
    if (m_ulCeReceived.size () > 0){
      m_ulCeReceived.erase (m_ulCeReceived.begin (), m_ulCeReceived.end ());
    }
  }
  else{
    // Send SR info to the scheduler
    NrMacSchedSapProvider::SchedUlSrInfoReqParameters params;
    params.m_snfSf = m_currentSlot;
    params.m_srList.insert (params.m_srList.begin(), m_srRntiList.begin (), m_srRntiList.end ());
    m_srRntiList.clear();

    m_macSchedSapProvider->SchedUlSrInfoReq (params);

    for (const auto & v : params.m_srList){
      Ptr<NrSRMessage> msg =  Create<NrSRMessage> ();
      msg->SetRNTI (v);
      m_macRxedCtrlMsgsTrace (m_currentSlot, GetCellId (), v, GetBwpId (), msg);
    }

    // Send UL BSR reports to the scheduler
    if (m_ulCeReceived.size () > 0){
      NrMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters ulMacReq;
      ulMacReq.m_sfnSf = sfnSf;
      ulMacReq.m_macCeList.insert (ulMacReq.m_macCeList.begin (), m_ulCeReceived.begin (), m_ulCeReceived.end ());
      m_ulCeReceived.erase (m_ulCeReceived.begin (), m_ulCeReceived.end ());
      m_macSchedSapProvider->SchedUlMacCtrlInfoReq (ulMacReq);

      for (const auto & v : ulMacReq.m_macCeList){
        Ptr<NrBsrMessage> msg = Create<NrBsrMessage> ();
        msg->SetBsr (v);
        m_macRxedCtrlMsgsTrace (m_currentSlot, GetCellId (), v.m_rnti, GetBwpId (), msg);
      }
    }
  }

  NrMacSchedSapProvider::SchedUlTriggerReqParameters ulParams;

  ulParams.m_snfSf = sfnSf;
  ulParams.m_slotType = type;

  // Forward UL HARQ feebacks collected during last TTI
  if (m_ulHarqInfoReceived.size () > 0){
    ulParams.m_ulHarqInfoList = m_ulHarqInfoReceived;
    // empty local buffer
    m_ulHarqInfoReceived.clear ();
  }

  m_macSchedSapProvider->SchedUlTriggerReq (ulParams);
}

void
NrGnbMac::SetForwardUpCallback (Callback <void, Ptr<Packet> > cb){
  m_forwardUpCallback = cb;
}

void
NrGnbMac::ReceiveBsrMessage  (MacCeElement bsr){
  NS_LOG_FUNCTION (this);
  // in order to use existing SAP interfaces we need to convert MacCeElement to MacCeListElement_s

  MacCeListElement_s mcle;
  mcle.m_rnti = bsr.m_rnti;
  mcle.m_macCeValue.m_bufferStatus = bsr.m_macCeValue.m_bufferStatus;
  mcle.m_macCeValue.m_crnti = bsr.m_macCeValue.m_crnti;
  mcle.m_macCeValue.m_phr = bsr.m_macCeValue.m_phr;
  mcle.m_macCeValue.m_bufferStatus = bsr.m_macCeValue.m_bufferStatus;

  if (bsr.m_macCeType == MacCeElement::BSR){
    mcle.m_macCeType = MacCeListElement_s::BSR;
  }
  else if (bsr.m_macCeType == MacCeElement::CRNTI){
    mcle.m_macCeType = MacCeListElement_s::CRNTI;
  }
  else if (bsr.m_macCeType == MacCeElement::PHR){
    mcle.m_macCeType = MacCeListElement_s::PHR;
  }

  m_ccmMacSapUser->UlReceiveMacCe (mcle, GetBwpId ());
}

void
NrGnbMac::DoReportMacCeToScheduler (MacCeListElement_s bsr){
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG (this << " bsr Size " << (uint16_t) m_ulCeReceived.size ());
  uint32_t size = 0;

  //send to LteCcmMacSapUser
  //convert MacCeListElement_s to MacCeElement

  MacCeElement mce;
  mce.m_rnti = bsr.m_rnti;
  mce.m_macCeValue.m_bufferStatus = bsr.m_macCeValue.m_bufferStatus;
  mce.m_macCeValue.m_crnti = bsr.m_macCeValue.m_crnti;
  mce.m_macCeValue.m_phr = bsr.m_macCeValue.m_phr;
  mce.m_macCeValue.m_bufferStatus = bsr.m_macCeValue.m_bufferStatus;

 if (bsr.m_macCeType == MacCeListElement_s::BSR){
    mce.m_macCeType = MacCeElement::BSR;
  }
 else if (bsr.m_macCeType == MacCeListElement_s::CRNTI){
    mce.m_macCeType = MacCeElement::CRNTI;
  }
 else if (bsr.m_macCeType == MacCeListElement_s::PHR){
    mce.m_macCeType = MacCeElement::PHR;
  }

 for (const auto & v : bsr.m_macCeValue.m_bufferStatus){
    size += v;
  }

  m_ulCeReceived.push_back (mce);   // this to called when LteUlCcmSapProvider::ReportMacCeToScheduler is called
  NS_LOG_DEBUG (" Reported by UE " << static_cast<uint32_t> (bsr.m_macCeValue.m_crnti) <<
                " size " << size << " bsr vector ize after push_back " <<
                static_cast<uint32_t> (m_ulCeReceived.size ()));
}

void
NrGnbMac::DoReportSrToScheduler (uint16_t rnti){
  NS_LOG_FUNCTION (this);
  m_srRntiList.push_back (rnti);
  m_srCallback (GetBwpId (), rnti);
}

void
NrGnbMac::DoReceivePhyPdu (Ptr<Packet> p){  // 물리 계층으로부터 넘겨 받는 곳
  NS_LOG_FUNCTION (this);

  LteRadioBearerTag tag;
  p->RemovePacketTag (tag);

  uint16_t rnti = tag.GetRnti ();
  auto rntiIt = m_rlcAttached.find (rnti);
  NS_ASSERT_MSG (rntiIt != m_rlcAttached.end (), "RNTI 찾을 수 없음" << rnti);
  PacketUeIdTag ueIdTag;
  if (p->RemovePacketTag (ueIdTag)) {
    uint32_t ueIndex = ueIdTag.GetUeId ();
    ueIndexToRnti[ueIndex] = rnti;      // 전역 map 에 저장
    rntiToUeIndex[rnti] = ueIndex;      // 혹은 역매핑도 원한다면
  }
  // Age가 가장 처음으로 gNB MAC 계층으로 도착하는 곳
  PacketCreationTimeTag creationTimeTag;                        // 패킷 생성 시간 태그 확인
  if (p->RemovePacketTag(creationTimeTag)){
    uint64_t 생성시간 = creationTimeTag.GetCreationTime();      // 패킷을 생성한 시간을 저장
    uint64_t 받은시간 = Simulator::Now().GetMicroSeconds();     // gNB가 패킷을 받은 시간을 저장
    uint64_t age = 받은시간 - 생성시간;                         // age는 "gNB가 받은 시간 - 패킷 생성 시간"의 차이로 계산

    m_패킷생성시간[rnti] = 생성시간;                            // 각 패킷의 생성 시간을 rnti(UE)에 매핑하여 저장
    m_패킷받은시간[rnti] = 받은시간;                            // 각 패킷의 받은 시간을 rnti(UE)에 매핑하여 저장

    PacketPriorityTag PriorityTag;                              // 패킷 우선 순위 태그 확인
    if (p->RemovePacketTag(PriorityTag)){
      uint32_t 우선순위 = PriorityTag.GetPriority();            // 패킷 우선순위를 저장
      m_패킷우선순위[rnti] = 우선순위;                          // 각 패킷의 우선 순위를 rnti(UE)에 매핑하여 저장

      NS_LOG_INFO("UE : " << rnti << "\t 우선순위 : " << 우선순위 << "\t Age : " << age << "\t gNB가 수신한 시점");
      auto &ageQueue = m_AgeQueues[rnti];                       // RNTI가 있으면 대응하는 큐를 꺼내고, 없으면 새로 생성
      AgeEntry entry;
      entry.priority = 우선순위;  // 우선순위
      entry.age      = age;       // 계산된 age
      entry.count    = 1;         // 새 엔트리의 count는 1로 초기화
      ageQueue.push(entry);
    }
  }

  // Try to peek whatever header; in the first byte there will be the LC ID.
  NrMacHeaderFsUl header;
  p->PeekHeader (header);

  // Based on LC ID, we know if it is a CE or simply data.
  if (header.GetLcId () == NrMacHeaderFsUl::SHORT_BSR){
    NrMacShortBsrCe bsrHeader;
    p->RemoveHeader (bsrHeader); // Really remove the header this time

    // Convert our custom header into the structure that the scheduler expects:
    MacCeElement bsr;

    bsr.m_macCeType = MacCeElement::BSR;
    bsr.m_rnti = rnti;
    bsr.m_macCeValue.m_bufferStatus.resize (4);
    bsr.m_macCeValue.m_bufferStatus[0] = bsrHeader.m_bufferSizeLevel_0;
    bsr.m_macCeValue.m_bufferStatus[1] = bsrHeader.m_bufferSizeLevel_1;
    bsr.m_macCeValue.m_bufferStatus[2] = bsrHeader.m_bufferSizeLevel_2;
    bsr.m_macCeValue.m_bufferStatus[3] = bsrHeader.m_bufferSizeLevel_3;

    ReceiveBsrMessage (bsr); // Here it will be converted again, but our job is done.
    return;
  }

  // Ok, we know it is data, so let's extract and pass to RLC.

  NrMacHeaderVs macHeader;
  p->RemoveHeader (macHeader);
  p->GetSize();
  auto lcidIt = rntiIt->second.find (macHeader.GetLcId ());

  // 처리량 계산
  uint64_t bytes = p->GetSize (); // 패킷 바이트 크기
  rntiToTotalBytes[rnti] += bytes;

  LteMacSapUser::ReceivePduParameters rxParams;
  rxParams.p = p;
  rxParams.lcid = macHeader.GetLcId ();
  rxParams.rnti = rnti;

  if (rxParams.p->GetSize ()){
    (*lcidIt).second->ReceivePdu (rxParams);
  }
}

void
NrGnbMac::PrintAverageThroughput (){
  std::cout << "----------[RNTI 당 평균 Throughput]----------" << std::endl;

  for (const auto &[rnti, totalBytes] : rntiToTotalBytes){
    double throughput = (totalBytes * 8.0) / 10.0; // bps
    std::cout << "RNTI: " << rnti << ", Throughput: " << throughput / 1e3 << " Kbps" << std::endl;
  }
}

void
NrGnbMac::PrintAverageAoI (){
  std::cout << "----------[가중 평균 AoI]----------" << std::endl;
  if (m_AoI.empty ()) {
    std::cout << "No AoI samples available\n";
    return;
  }
  double sum = std::accumulate(m_AoI.begin(), m_AoI.end(), 0.0);
  double avg = sum / static_cast<double>(m_AoI.size());
  std::cout << "평균 AoI : " << m_AoI.size() << " 샘플 수 : " << avg << " μs\n";
}

NrGnbPhySapUser*
NrGnbMac::GetPhySapUser (){
  return m_phySapUser;
}

void
NrGnbMac::SetPhySapProvider (NrPhySapProvider* ptr){
  m_phySapProvider = ptr;
}

NrMacSchedSapUser*
NrGnbMac::GetNrMacSchedSapUser (){
  return m_macSchedSapUser;
}

void
NrGnbMac::SetNrMacSchedSapProvider (NrMacSchedSapProvider* ptr){
  m_macSchedSapProvider = ptr;
}

NrMacCschedSapUser*
NrGnbMac::GetNrMacCschedSapUser (){
  return m_macCschedSapUser;
}

void
NrGnbMac::SetNrMacCschedSapProvider (NrMacCschedSapProvider* ptr){
  m_macCschedSapProvider = ptr;
}

void
NrGnbMac::DoUlCqiReport (NrMacSchedSapProvider::SchedUlCqiInfoReqParameters ulcqi){
  if (ulcqi.m_ulCqi.m_type == UlCqiInfo::PUSCH){
    NS_LOG_DEBUG (this << " gNB rxed an PUSCH UL-CQI");
  }
  else if (ulcqi.m_ulCqi.m_type == UlCqiInfo::SRS){
    NS_LOG_DEBUG (this << " gNB rxed an SRS UL-CQI");
  }
  // UL CQI SINR 보고
  NS_LOG_INFO ("*** UL CQI report SINR " << LteFfConverter::fpS11dot3toDouble (ulcqi.m_ulCqi.m_sinr[0]) << " slot: " << m_currentSlot);

  // NS_ASSERT (ulcqi.m_sfnSf.m_varTtiNum != 0); Now UL data can be the first TTI..
  m_ulCqiReceived.push_back (ulcqi);
}

void
NrGnbMac::DoReceiveControlMessage (Ptr<NrControlMessage> msg){
  NS_LOG_FUNCTION (this << msg);

  switch (msg->GetMessageType ()){
    case(NrControlMessage::CGR):{
      // Configured Grant Request
       Ptr<NrCGRMessage> cgr = DynamicCast<NrCGRMessage> (msg);
       componentCarrierId_configuredGrant = GetBwpId();
       lcid_configuredGrant = cgr->GetLCID();
       Time trafficInAndEncode = cgr->GetTrafficTimeInit() + m_phySapProvider->GetTbUlEncodeLatency();
       m_ccmMacSapUser-> UlReceiveCgr (cgr->GetRNTI (), GetBwpId (), cgr->GetBufSize(), cgr->GetLCID(), cgr->GetTrafficP(), cgr->GetTrafficTimeInit(), cgr->GetTrafficDeadline());
       break;
     }
    case (NrControlMessage::SR):{
      // Report it to the CCM. Then he will call the right MAC
      Ptr<NrSRMessage> sr = DynamicCast<NrSRMessage> (msg);
      m_ccmMacSapUser->UlReceiveSr (sr->GetRNTI (), GetBwpId ());
      break;
    }
    case (NrControlMessage::DL_CQI):{
      Ptr<NrDlCqiMessage> cqi = DynamicCast<NrDlCqiMessage> (msg);
      DlCqiInfo cqiElement = cqi->GetDlCqi ();
      NS_ASSERT (cqiElement.m_rnti != 0);
      m_dlCqiReceived.push_back (cqiElement);
      break;
    }
    case (NrControlMessage::DL_HARQ):{
      Ptr<NrDlHarqFeedbackMessage> dlharq = DynamicCast<NrDlHarqFeedbackMessage> (msg);
      DoDlHarqFeedback (dlharq->GetDlHarqFeedback ());
      break;
    }
    default:
      NS_LOG_INFO ("Control message not supported/expected");
  }

}

void
NrGnbMac::DoUlHarqFeedback (const UlHarqInfo &params){
  NS_LOG_FUNCTION (this);
  m_ulHarqInfoReceived.push_back (params);
}

void
NrGnbMac::DoDlHarqFeedback (const DlHarqInfo &params){
  NS_LOG_FUNCTION (this);
  // Update HARQ buffer
  std::unordered_map <uint16_t, NrDlHarqProcessesBuffer_t>::iterator it =  m_miDlHarqProcessesPackets.find (params.m_rnti);
  NS_ASSERT (it != m_miDlHarqProcessesPackets.end ());

  for (uint8_t stream = 0; stream < params.m_harqStatus.size (); stream++){
    if (params.m_harqStatus.at (stream) == DlHarqInfo::ACK){
      // discard buffer
      Ptr<PacketBurst> emptyBuf = CreateObject <PacketBurst> ();
      (*it).second.at (params.m_harqProcessId).m_infoPerStream.at (stream).m_pktBurst = emptyBuf;
      NS_LOG_DEBUG (this << " HARQ-ACK UE " << params.m_rnti << " harqId " << (uint16_t)params.m_harqProcessId << " stream id " << static_cast<uint16_t> (stream));
    }
    else if (params.m_harqStatus.at (stream) == DlHarqInfo::NACK){
      NS_LOG_DEBUG (this << " HARQ-NACK UE " << params.m_rnti << " harqId " << (uint16_t)params.m_harqProcessId << " stream id " << static_cast<uint16_t> (stream));
    }
    else if (params.m_harqStatus.at (stream) == DlHarqInfo::NONE){
      NS_LOG_DEBUG (this << " HARQ-NONE UE " << params.m_rnti << " harqId " << (uint16_t)params.m_harqProcessId << " stream id " << static_cast<uint16_t> (stream));
    }
    else{
      NS_FATAL_ERROR (" HARQ functionality not implemented");
    }
  }

  /* trace for HARQ feedback*/
  m_dlHarqFeedback (params);
  m_dlHarqInfoReceived.push_back (params);
}

void
NrGnbMac::DoReportBufferStatus (LteMacSapProvider::ReportBufferStatusParameters params){
  NS_LOG_FUNCTION (this);
  NrMacSchedSapProvider::SchedDlRlcBufferReqParameters schedParams;
  schedParams.m_logicalChannelIdentity = params.lcid;
  schedParams.m_rlcRetransmissionHolDelay = params.retxQueueHolDelay;
  schedParams.m_rlcRetransmissionQueueSize = params.retxQueueSize;
  schedParams.m_rlcStatusPduSize = params.statusPduSize;
  schedParams.m_rlcTransmissionQueueHolDelay = params.txQueueHolDelay;
  schedParams.m_rlcTransmissionQueueSize = params.txQueueSize;
  schedParams.m_rnti = params.rnti;

  m_macSchedSapProvider->SchedDlRlcBufferReq (schedParams);
}

// forwarded from LteMacSapProvider
void
NrGnbMac::DoTransmitPdu (LteMacSapProvider::TransmitPduParameters params){
  // TB UID passed back along with RLC data as HARQ process ID
  uint32_t tbMapKey = ((params.rnti & 0xFFFF) << 8) | (params.harqProcessId & 0xFF);
  auto harqIt = m_miDlHarqProcessesPackets.find (params.rnti);
  auto it = m_macPduMap.find (tbMapKey);

  if (it == m_macPduMap.end ()){
    NS_FATAL_ERROR ("No MAC PDU storage element found for this TB UID/RNTI");
  }

  NrMacHeaderVs header;
  header.SetLcId (params.lcid);
  header.SetSize (params.pdu->GetSize ());

  params.pdu->AddHeader (header);

  LteRadioBearerTag bearerTag (params.rnti, params.lcid, params.layer);
  params.pdu->AddPacketTag (bearerTag);

  harqIt->second.at (params.harqProcessId).m_infoPerStream.at (params.layer).m_pktBurst->AddPacket (params.pdu);

  it->second.m_used += params.pdu->GetSize ();
  NS_ASSERT_MSG (it->second.m_maxBytes >= it->second.m_used, "DCI OF " << it->second.m_maxBytes << " total used " << it->second.m_used);

  m_phySapProvider->SendMacPdu (params.pdu, it->second.m_sfnSf, it->second.m_dci->m_symStart, params.layer);
}

void
NrGnbMac::SendRar (const std::vector <BuildRarListElement_s> &rarList){
  NS_LOG_FUNCTION (this);

  // Random Access procedure: send RARs
  Ptr<NrRarMessage> rarMsg = Create<NrRarMessage> ();
  uint16_t raRnti = 1; // NO!! 38.321-5.1.3
  rarMsg->SetRaRnti (raRnti);
  rarMsg->SetSourceBwp (GetBwpId ());
  for (const auto & rarAllocation : rarList){
    auto itRapId = m_rapIdRntiMap.find (rarAllocation.m_rnti);
    NS_ABORT_IF (itRapId == m_rapIdRntiMap.end ());
    NrRarMessage::Rar rar;
    rar.rapId = itRapId->second;
    rar.rarPayload = rarAllocation;
    rarMsg->AddRar (rar);
    // NS_LOG_INFO ("In slot " << m_currentSlot <<
    //              " send to PHY the RAR message for RNTI " <<
    //              rarAllocation.m_rnti << " rapId " << itRapId->second);
    m_macTxedCtrlMsgsTrace (m_currentSlot, GetCellId (), rarAllocation.m_rnti, GetBwpId (), rarMsg);
  }

  if (rarList.size () > 0){
    m_phySapProvider->SendControlMessage (rarMsg);
    m_rapIdRntiMap.clear ();
  }

}

void NrGnbMac::DoSchedConfigIndication (NrMacSchedSapUser::SchedConfigIndParameters ind){
  NS_ASSERT (ind.m_sfnSf.GetNumerology () == m_currentSlot.GetNumerology ());
  std::sort (ind.m_slotAllocInfo.m_varTtiAllocInfo.begin (), ind.m_slotAllocInfo.m_varTtiAllocInfo.end ());

  NS_LOG_DEBUG ("Received from scheduler a new allocation: " << ind.m_slotAllocInfo);

  m_phySapProvider->SetSlotAllocInfo (ind.m_slotAllocInfo);

  SendRar (ind.m_buildRarList);

  // Age 값 수집을 위해 벡터 초기화
  // m_AoI.clear();

  for (unsigned islot = 0; islot < ind.m_slotAllocInfo.m_varTtiAllocInfo.size (); islot++){
    VarTtiAllocInfo &varTtiAllocInfo = ind.m_slotAllocInfo.m_varTtiAllocInfo[islot];
    
    uint16_t rnti = varTtiAllocInfo.m_dci->m_rnti;

    auto& ageQueue = m_AgeQueues[rnti];
    if (!ageQueue.empty()){
      AgeEntry &entry = ageQueue.front();

      uint64_t aoi = entry.age;                      // AoI 계산

      // 스케줄러에 Age 값을 전달하여 우선순위 결정에 반영
      varTtiAllocInfo.m_age = aoi;

      // AoI 값을 로그로 출력
      NS_LOG_INFO("UE " << rnti << " \tAoI 값 = " << varTtiAllocInfo.m_age << " \t 스케줄링된 후 시점");
      ageQueue.pop(); // 처리 완료 후 제거
      
      m_AoI.push_back(aoi);  // 스케줄링된 Aoi 값을 벡터에 추가
    }
    else{
      varTtiAllocInfo.m_age = 1;  // Age 정보가 없으면 기본값 1
    }

    if (varTtiAllocInfo.m_dci->m_type != DciInfoElementTdma::CTRL && varTtiAllocInfo.m_dci->m_format == DciInfoElementTdma::DL){
      uint16_t rnti = varTtiAllocInfo.m_dci->m_rnti;
      auto rntiIt = m_rlcAttached.find (rnti);
      //NS_ABORT_MSG_IF(rntiIt == m_rlcAttached.end (), "Scheduled UE " << rntiIt->first << " not attached");

      // Call RLC entities to generate RLC PDUs
      auto dciElem = varTtiAllocInfo.m_dci;
      uint8_t tbUid = dciElem->m_harqProcess;

      std::unordered_map <uint16_t, NrDlHarqProcessesBuffer_t>::iterator harqIt = m_miDlHarqProcessesPackets.find (rnti);
      NS_ASSERT (harqIt != m_miDlHarqProcessesPackets.end ());

      std::pair <std::unordered_map<uint32_t, struct NrMacPduInfo>::iterator, bool> mapRet;

      //for new data first force emptying correspondent harq pkt buffer
      for (uint8_t stream = 0; stream < dciElem->m_ndi.size (); stream++){
        if (dciElem->m_ndi.at (stream) == 1){
          NS_ASSERT (dciElem->m_tbSize.at (stream) > 0);
          //if any of the stream is carrying new data
          //we refresh the info for all the streams in the
          //HARQ buffer.
          for (auto &it:harqIt->second.at (tbUid).m_infoPerStream){
            Ptr<PacketBurst> pb = CreateObject <PacketBurst> ();
            it.m_pktBurst = pb;
            it.m_lcidList.clear ();
          }
          //now push the NrMacPduInfo into m_macPduMap
          //which would be used to extract info while
          //giving the PDU to the PHY in DoTransmitPdu.
          //it is done for only new data.
          NrMacPduInfo macPduInfo (ind.m_sfnSf, dciElem);
          //insert into MAC PDU map
          uint32_t tbMapKey = ((rnti & 0xFFFF) << 8) | (tbUid & 0xFF);
          mapRet = m_macPduMap.insert (std::pair<uint32_t, struct NrMacPduInfo> (tbMapKey, macPduInfo));
          if (!mapRet.second){
            NS_FATAL_ERROR ("MAC PDU map element exists");
          }
          break;
        }
      }

      //for each LC j
      for (uint16_t j = 0; j < varTtiAllocInfo.m_rlcPduInfo.size (); j++){
        //for each stream k of LC j
        for (uint8_t k = 0; k < varTtiAllocInfo.m_rlcPduInfo.at (j).size (); k++){
          if (dciElem->m_ndi.at (k) == 1){
            auto rlcPduInfo = varTtiAllocInfo.m_rlcPduInfo.at (j).at (k);
            std::unordered_map<uint8_t, LteMacSapUser*>::iterator lcidIt = rntiIt->second.find (rlcPduInfo.m_lcid);
            NS_ASSERT_MSG (lcidIt != rntiIt->second.end (), "could not find LCID" << rlcPduInfo.m_lcid);
            NS_LOG_DEBUG ("Notifying RLC of TX opportunity for TB " << (unsigned int)tbUid
                          << " LC ID " << +rlcPduInfo.m_lcid << " stream " << +k
                          << " size " << (unsigned int) rlcPduInfo.m_size << " bytes");

            (*lcidIt).second->NotifyTxOpportunity (LteMacSapUser::TxOpportunityParameters ((rlcPduInfo.m_size), k, tbUid, GetBwpId (), rnti, rlcPduInfo.m_lcid));
            harqIt->second.at (tbUid).m_infoPerStream.at (k).m_lcidList.push_back (rlcPduInfo.m_lcid);
          }
          else{
            if (varTtiAllocInfo.m_dci->m_tbSize.at (k) > 0){
              Ptr<PacketBurst> pb = harqIt->second.at (tbUid).m_infoPerStream.at (k).m_pktBurst;
              for (std::list<Ptr<Packet> >::const_iterator j = pb->Begin (); j != pb->End (); ++j){
                Ptr<Packet> pkt = (*j)->Copy ();
                m_phySapProvider->SendMacPdu (pkt, ind.m_sfnSf, dciElem->m_symStart, k);
              }
            }
          }
        }
      }

      if (mapRet.second){
        m_macPduMap.erase (mapRet.first);    // delete map entry
      }

      for (uint8_t stream = 0; stream < dciElem->m_tbSize.size (); stream++){
        NrSchedulingCallbackInfo traceInfo;
        traceInfo.m_frameNum = ind.m_sfnSf.GetFrame ();
        traceInfo.m_subframeNum = ind.m_sfnSf.GetSubframe ();
        traceInfo.m_slotNum = ind.m_sfnSf.GetSlot ();
        traceInfo.m_symStart = dciElem->m_symStart;
        traceInfo.m_numSym = dciElem->m_numSym;
        traceInfo.m_streamId = stream;
        traceInfo.m_tbSize = dciElem->m_tbSize.at (stream);
        traceInfo.m_mcs = dciElem->m_mcs.at (stream);
        traceInfo.m_rnti = dciElem->m_rnti;
        traceInfo.m_bwpId = GetBwpId ();
        traceInfo.m_ndi = dciElem->m_ndi.at (stream);
        traceInfo.m_rv = dciElem->m_rv.at (stream);
        traceInfo.m_harqId = dciElem->m_harqProcess;

        m_dlScheduling (traceInfo);
      }
    }
    else if (varTtiAllocInfo.m_dci->m_type != DciInfoElementTdma::CTRL && varTtiAllocInfo.m_dci->m_type != DciInfoElementTdma::SRS && varTtiAllocInfo.m_dci->m_format == DciInfoElementTdma::UL){
      //UL scheduling info trace
      // Call RLC entities to generate RLC PDUs
      auto dciElem = varTtiAllocInfo.m_dci;
      for (uint8_t stream = 0; stream < dciElem->m_tbSize.size (); stream++){
        NrSchedulingCallbackInfo traceInfo;
        traceInfo.m_frameNum = ind.m_sfnSf.GetFrame ();
        traceInfo.m_subframeNum = ind.m_sfnSf.GetSubframe ();
        traceInfo.m_slotNum = ind.m_sfnSf.GetSlot ();
        traceInfo.m_symStart = dciElem->m_symStart;
        traceInfo.m_numSym = dciElem->m_numSym;
        traceInfo.m_streamId = stream;
        traceInfo.m_tbSize = dciElem->m_tbSize.at (stream);
        traceInfo.m_mcs = dciElem->m_mcs.at (stream);
        traceInfo.m_rnti = dciElem->m_rnti;
        traceInfo.m_bwpId = GetBwpId ();
        traceInfo.m_ndi = dciElem->m_ndi.at (stream);
        traceInfo.m_rv = dciElem->m_rv.at (stream);
        traceInfo.m_harqId = dciElem->m_harqProcess;

        m_ulScheduling (traceInfo);
      }
    }
  }
  // 스케줄링 후 평균 Age 계산
  if (!m_AoI.empty())
  {
    uint64_t sumAge = std::accumulate(m_AoI.begin(), m_AoI.end(), uint64_t(0));
    uint64_t avgAge = sumAge / m_AoI.size();
    NS_LOG_INFO("\n스케줄링 후 평균 Age 값 : " << avgAge);
  }

}

// ////////////////////////////////////////////
// CMAC SAP
// ////////////////////////////////////////////

void
NrGnbMac::DoConfigureMac (uint16_t ulBandwidth, uint16_t dlBandwidth){
  NS_LOG_FUNCTION (this);

  // The bandwidth arrived in Hz. We need to know it in number of RB, and then
  // consider how many RB are inside a single RBG.
  uint16_t bw_in_rbg = m_phySapProvider->GetRbNum () / GetNumRbPerRbg ();
  m_bandwidthInRbg = bw_in_rbg;

  NS_LOG_DEBUG ("Mac configured. Attributes:" << std::endl <<
                "\t NumRbPerRbg: " << m_numRbPerRbg << std::endl <<
                "\t NumHarqProcess: " << +m_numHarqProcess << std::endl <<
                "Physical properties: " << std::endl <<
                "\t Bandwidth provided: " << ulBandwidth * 1000 * 100 << " Hz" << std::endl <<
                "\t that corresponds to " << bw_in_rbg << " RBG, as we have " <<
                m_phySapProvider->GetRbNum () << " RB and " << GetNumRbPerRbg () <<
                " RB per RBG");

  NrMacCschedSapProvider::CschedCellConfigReqParameters params;

  params.m_ulBandwidth = m_bandwidthInRbg;
  params.m_dlBandwidth = m_bandwidthInRbg;

  m_macCschedSapProvider->CschedCellConfigReq (params);
}

void
NrGnbMac::BeamChangeReport (BeamConfId beamConfId, uint8_t rnti){
  NrMacCschedSapProvider::CschedUeConfigReqParameters params;
  params.m_rnti = rnti;
  params.m_beamConfId = beamConfId;
  params.m_transmissionMode = 0;   // set to default value (SISO) for avoiding random initialization (valgrind error)
  m_macCschedSapProvider->CschedUeConfigReq (params);
}

uint16_t
NrGnbMac::GetBwpId () const{
  if (m_phySapProvider){
    return m_phySapProvider->GetBwpId ();
  }
 else{
    return UINT16_MAX;
  }

}

uint16_t
NrGnbMac::GetCellId () const{
  if (m_phySapProvider){
    return m_phySapProvider->GetCellId ();
  }
  else{
    return UINT16_MAX;
  }

}

std::shared_ptr<DciInfoElementTdma>
NrGnbMac::GetDlCtrlDci () const{
  NS_LOG_FUNCTION (this);

  auto bwInRbg = m_phySapProvider->GetRbNum () / GetNumRbPerRbg ();
  NS_ASSERT (bwInRbg > 0);
  std::vector<uint8_t> rbgBitmask (bwInRbg , 1);

  return std::make_shared<DciInfoElementTdma> (0, m_macSchedSapProvider->GetDlCtrlSyms (),
                                                DciInfoElementTdma::DL, DciInfoElementTdma::CTRL,
                                                rbgBitmask);
}

std::shared_ptr<DciInfoElementTdma>
NrGnbMac::GetUlCtrlDci () const{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (m_bandwidthInRbg > 0);
  std::vector<uint8_t> rbgBitmask (m_bandwidthInRbg , 1);

  return std::make_shared<DciInfoElementTdma> (0, m_macSchedSapProvider->GetUlCtrlSyms (),
                                                DciInfoElementTdma::UL, DciInfoElementTdma::CTRL,
                                                rbgBitmask);
}

void
NrGnbMac::DoAddUe (uint16_t rnti){
  NS_LOG_FUNCTION (this << " rnti=" << rnti);
  std::unordered_map<uint8_t, LteMacSapUser*> empty;
  std::pair <std::unordered_map <uint16_t, std::unordered_map<uint8_t, LteMacSapUser*> >::iterator, bool>
  ret = m_rlcAttached.insert (std::pair <uint16_t,  std::unordered_map<uint8_t, LteMacSapUser*> >
                                (rnti, empty));
  NS_ASSERT_MSG (ret.second, "element already present, RNTI already existed");
  
  // 패킷 생성 시간 및 수신 시간 초기화
  m_패킷생성시간[rnti] = 0;
  m_패킷받은시간[rnti] = 0;

  // UE의 값 초기화 (초기값 priority=1, age=1, count=1)
  m_AgeQueues[rnti] = {};
  AgeEntry initEntry;
  initEntry.priority = 1;
  initEntry.age      = 1;
  initEntry.count    = 1;            
  m_AgeQueues[rnti].push(initEntry); // 초기화

  NrMacCschedSapProvider::CschedUeConfigReqParameters params;
  params.m_rnti = rnti;
  params.m_beamConfId = m_phySapProvider->GetBeamConfId (rnti);
  params.m_transmissionMode = 0;   // set to default value (SISO) for avoiding random initialization (valgrind error)
  m_macCschedSapProvider->CschedUeConfigReq (params);

  // Create DL transmission HARQ buffers
  NrDlHarqProcessesBuffer_t buf;
  uint16_t harqNum = GetNumHarqProcess ();
  uint16_t numStreams = 2;
  buf.resize (harqNum);
  for (uint8_t i = 0; i < harqNum; i++)
    {
      //for each of the HARQ process we have the info of max 2 streams
      for (uint16_t stream = 0; stream < numStreams; stream++)
        {
          HarqProcessInfoSingleStream info;
          Ptr<PacketBurst> pb = CreateObject <PacketBurst> ();
          info.m_pktBurst = pb;
          buf.at (i).m_infoPerStream.push_back (info);
        }
    }
  m_miDlHarqProcessesPackets.insert (std::pair <uint16_t, NrDlHarqProcessesBuffer_t> (rnti, buf));
  // ActivateUe(rnti);
  m_cgrConfigured[rnti] = false;
  m_nextCgrSlot[rnti] = SfnSf (0, 0, 0, m_currentSlot.GetNumerology ());
  m_posCgrIndex[rnti] = 0;
}

void
NrGnbMac::DoRemoveUe (uint16_t rnti){
  NS_LOG_FUNCTION (this << " rnti=" << rnti);
  NrMacCschedSapProvider::CschedUeReleaseReqParameters params;
  params.m_rnti = rnti;
  m_macCschedSapProvider->CschedUeReleaseReq (params);
  m_miDlHarqProcessesPackets.erase (rnti);
  m_rlcAttached.erase (rnti);
}

void
NrGnbMac::DoAddLc (LteEnbCmacSapProvider::LcInfo lcinfo, LteMacSapUser* msu){
  NS_LOG_FUNCTION (this);
  NS_LOG_FUNCTION (this);

  LteFlowId_t flow (lcinfo.rnti, lcinfo.lcId);

  std::unordered_map <uint16_t, std::unordered_map<uint8_t, LteMacSapUser*> >::iterator rntiIt = m_rlcAttached.find (lcinfo.rnti);
  NS_ASSERT_MSG (rntiIt != m_rlcAttached.end (), "RNTI not found");
  std::unordered_map<uint8_t, LteMacSapUser*>::iterator lcidIt = rntiIt->second.find (lcinfo.lcId);
  if (lcidIt == rntiIt->second.end ()){
      rntiIt->second.insert (std::pair<uint8_t, LteMacSapUser*> (lcinfo.lcId, msu));
    }
  else{
      NS_LOG_ERROR ("LC already exists");
    }

  // CCCH (LCID 0) is pre-configured
  // see FF LTE MAC Scheduler
  // Interface Specification v1.11,
  // 4.3.4 logicalChannelConfigListElement
  if (lcinfo.lcId != 0){
    struct NrMacCschedSapProvider::CschedLcConfigReqParameters params;
    params.m_rnti = lcinfo.rnti;
    params.m_reconfigureFlag = false;

    struct LogicalChannelConfigListElement_s lccle;
    lccle.m_logicalChannelIdentity = lcinfo.lcId;
    lccle.m_logicalChannelGroup = lcinfo.lcGroup;
    lccle.m_direction = LogicalChannelConfigListElement_s::DIR_BOTH;
    lccle.m_qosBearerType = lcinfo.isGbr ? LogicalChannelConfigListElement_s::QBT_GBR : LogicalChannelConfigListElement_s::QBT_NON_GBR;
    lccle.m_qci = lcinfo.qci;
    lccle.m_eRabMaximulBitrateUl = lcinfo.mbrUl;
    lccle.m_eRabMaximulBitrateDl = lcinfo.mbrDl;
    lccle.m_eRabGuaranteedBitrateUl = lcinfo.gbrUl;
    lccle.m_eRabGuaranteedBitrateDl = lcinfo.gbrDl;
    params.m_logicalChannelConfigList.push_back (lccle);

    m_macCschedSapProvider->CschedLcConfigReq (params);
  }
}

void
NrGnbMac::DoReconfigureLc (LteEnbCmacSapProvider::LcInfo lcinfo){
  NS_FATAL_ERROR ("not implemented");
}

void
NrGnbMac::DoReleaseLc (uint16_t rnti, uint8_t lcid){
  //Find user based on rnti and then erase lcid stored against the same
  std::unordered_map <uint16_t, std::unordered_map<uint8_t, LteMacSapUser*> >::iterator rntiIt = m_rlcAttached.find (rnti);
  rntiIt->second.erase (lcid);

  struct NrMacCschedSapProvider::CschedLcReleaseReqParameters params;
  params.m_rnti = rnti;
  params.m_logicalChannelIdentity.push_back (lcid);
  m_macCschedSapProvider->CschedLcReleaseReq (params);
}

void
NrGnbMac::UeUpdateConfigurationReq (LteEnbCmacSapProvider::UeConfig params){
  NS_LOG_FUNCTION (this);
  // propagates to scheduler
  NrMacCschedSapProvider::CschedUeConfigReqParameters req;
  req.m_rnti = params.m_rnti;
  req.m_transmissionMode = params.m_transmissionMode;
  req.m_beamConfId = m_phySapProvider->GetBeamConfId (params.m_rnti);
  req.m_reconfigureFlag = true;
  m_macCschedSapProvider->CschedUeConfigReq (req);
}

LteEnbCmacSapProvider::RachConfig
NrGnbMac::DoGetRachConfig (){
  //UEs in NR does not choose RACH preambles randomly, therefore,
  //it does not rely on the following parameters. However, the
  //recent change in LteUeRrc class introduced an assert to
  //check the correct value of connEstFailCount parameter.
  //Thus, we need to assign dummy but correct values to
  //avoid this assert in LteUeRrc class.
  LteEnbCmacSapProvider::RachConfig rc;
  rc.numberOfRaPreambles = 52;
  rc.preambleTransMax = 50;
  rc.raResponseWindowSize = 3;
  rc.connEstFailCount = 1;
  return rc;
}

LteEnbCmacSapProvider::AllocateNcRaPreambleReturnValue
NrGnbMac::DoAllocateNcRaPreamble (uint16_t rnti){
  return LteEnbCmacSapProvider::AllocateNcRaPreambleReturnValue ();
}

// ////////////////////////////////////////////
// CSCHED SAP
// ////////////////////////////////////////////


void
NrGnbMac::DoCschedCellConfigCnf (NrMacCschedSapUser::CschedCellConfigCnfParameters params){
  NS_LOG_FUNCTION (this);
}

void
NrGnbMac::DoCschedUeConfigCnf (NrMacCschedSapUser::CschedUeConfigCnfParameters params){
  NS_LOG_FUNCTION (this);
}

void
NrGnbMac::DoCschedLcConfigCnf (NrMacCschedSapUser::CschedLcConfigCnfParameters params){
  NS_LOG_FUNCTION (this);
  // Call the CSCHED primitive
  // m_cschedSap->LcConfigCompleted();
}

void
NrGnbMac::DoCschedLcReleaseCnf (NrMacCschedSapUser::CschedLcReleaseCnfParameters params){
  NS_LOG_FUNCTION (this);
}

void
NrGnbMac::DoCschedUeReleaseCnf (NrMacCschedSapUser::CschedUeReleaseCnfParameters params){
  NS_LOG_FUNCTION (this);
}

void
NrGnbMac::DoCschedUeConfigUpdateInd (NrMacCschedSapUser::CschedUeConfigUpdateIndParameters params){
  NS_LOG_FUNCTION (this);
  // propagates to RRC
  LteEnbCmacSapUser::UeConfig ueConfigUpdate;
  ueConfigUpdate.m_rnti = params.m_rnti;
  ueConfigUpdate.m_transmissionMode = params.m_transmissionMode;
  m_cmacSapUser->RrcConfigurationUpdateInd (ueConfigUpdate);
}

void
NrGnbMac::DoCschedCellConfigUpdateInd (NrMacCschedSapUser::CschedCellConfigUpdateIndParameters params){
  NS_LOG_FUNCTION (this);
}

//Configured grant
uint8_t
NrGnbMac::GetConfigurationTime () const{
  return m_configurationTime;
}

void
NrGnbMac::SetConfigurationTime (uint8_t v){
  m_configurationTime = v;
}

void
NrGnbMac::SetCG (bool CGsch){
  m_cgScheduling = CGsch;
}

bool
NrGnbMac::GetCG () const{
  return m_cgScheduling;
}

void
NrGnbMac::DoReportCgrToScheduler (uint16_t rnti, uint32_t bufSize, uint8_t lcid, uint8_t traffP, Time traffInit, Time traffDeadline){
  NS_LOG_FUNCTION (this);
  ActivateUe(rnti);
  m_srRntiList.push_back (rnti);
  m_srCallback (GetBwpId (), rnti);
  m_cgrBufSizeList.push_back (bufSize);
  lcid_configuredGrant = lcid;
  m_cgrTraffP.push_back(traffP);
  m_cgrTraffInit.push_back(traffInit);
  m_cgrTraffDeadline.push_back(traffDeadline);
  m_cgrConfigured[rnti]  = false;
  m_cgrParams[rnti] = CgrParams {
    bufSize,
    traffP,
    traffInit,
    traffDeadline
  };
}

}