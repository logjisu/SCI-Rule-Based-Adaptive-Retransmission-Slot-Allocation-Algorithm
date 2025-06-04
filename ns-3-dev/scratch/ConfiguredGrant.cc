/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2020 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
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

/*
 * 설명: 이 코드는 UE에서 gNB로 n 패킷을 전송합니다. 동적 또는 Configured-Grant(CG) 스케줄러와 함께 작동합니다.
 * (두 스케줄러 모두 동시에 작동할 수 없습니다.)
 * 
 * CG의 경우 구성 시간이 선택됩니다. 이 시간에 UE는 요구 사항을 gNB로 전송합니다.
 * gNB는 각 UE에 대한 CG를 생성합니다.
 *
 * OFDMA 또는 TDMA 액세스 모드를 사용할 수 있습니다.
 * OFDMA 액세스 모드에 사용할 두 가지 새로운 스케줄링 정책이 포함되어 있습니다.
 *
 * 이 코드는 "cttc-3gpp-channel-simple-ran.cc"(5G-LENA) 코드를 기반으로 작성되었습니다.
 */

#include "ns3/a-packet-tags.h"
#include "ns3/antenna-module.h"
#include "ns3/buildings-helper.h"
#include "ns3/building-container.h"
#include "ns3/core-module.h"
#include "ns3/config-store.h"
#include <cstdlib>
#include <ctime>
#include "ns3/eps-bearer-tag.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-module.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/log.h"
#include <sstream>

using namespace ns3;

/*
 * "ConfiguredGrant" 구성 요소를 활성화하여 파일의 로그를 활성화합니다.
 * 다음과 같은 방식으로: export NS_LOG="ConfiguredGrant=level_info|prefix_func|prefix_time:NrMacSchedulerOfdmaPF=level_info|prefix_func|prefix_time"
 */
NS_LOG_COMPONENT_DEFINE ("ConfiguredGrant");

/*
 * 전역 변수
 */
Time g_txPeriod = Seconds(0.1);
Time delay;
std::fstream m_ScenarioFile;
static bool g_rxPdcpCallbackCalled = false;     // PDCP 수신 성공 False로 설정
static bool g_rxRxRlcPDUCallbackCalled = false; // RLC 수신 성공 False로 설정
static Ptr<NrGnbMac> gnbMacPtr;                 // gNB MAC 포인터
static std::vector<Ptr<NrUeNetDevice>> ueDevs;  // 각 UE용 NrUeNetDevice 포인터
/*
 * MyModel 클래스는 UE에서 gNB로 패킷을 전송하는 이벤트를 생성하는 기능이 포함되어 있습니다.
*/

class MyModel : public Application{
public:
  MyModel ();
  virtual ~MyModel();

  void Setup (Ptr<NetDevice> device, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, uint8_t period, uint32_t deadline);

  void StopModel() {
    m_running = false;
    if (m_sendEvent.IsRunning()) {
      Simulator::Cancel(m_sendEvent);
    }
  }

  // 하향링크(DL)
  void SendPacketDl ();
  void ScheduleTxDl ();

  // 상향링크(UL)
  void SendPacketUl ();
  void ScheduleTxUl (uint8_t period);
  void ScheduleTxUl_Configuration();

  void SetPriority (uint8_t p) { m_priority = p; }
  void SetUeIndex (uint16_t idx) { m_ueIndex = idx; }

private:
  Ptr<NetDevice>  m_device;     // UE, gNB 네트워크 장치
  Address         m_addr;       // 네트워크 주소(IP 주소, MAC 주소 등)
  uint32_t        m_packetSize; // 패킷의 크기(Byte 단위)
  uint32_t        m_nPackets;   // 패킷 전송 수
  DataRate        m_dataRate;   // 데이터 전송 속도
  EventId         m_sendEvent;  // 패킷 전송 시 타이머 이벤트
  bool            m_running;    // 패킷 전송 여부
  uint32_t        m_packetsSent;// 전송한 패킷 수
  uint8_t         m_periodicity;// 전송 주기
  uint32_t        m_deadline;   // gNB로 전달되어야 하는 최대 지연 시간
  uint8_t         m_priority;   // 우선순위
  bool            m_isConfigured;// CG 구성 여부
  uint16_t        m_ueIndex;    // UE 인덱스             
};

MyModel::MyModel ()
  : m_device(),
    m_addr (),
    m_packetSize (0),
    m_nPackets (0),
    m_dataRate (0),
    m_sendEvent (),
    m_running (false),
    m_packetsSent (0),
    m_periodicity(0),
    m_deadline(0)
{
}

MyModel::~MyModel(){

}


void
MyModel::Setup (Ptr<NetDevice> device, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, uint8_t period, uint32_t deadline) {
  m_device = device;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
  m_running = true;
  m_packetsSent = 0;
  m_periodicity = period;
  m_deadline = deadline;
  m_isConfigured = false;
}

static std::vector<Ptr<MyModel>> ueModels;
static std::vector<uint32_t> inactive, active;

/*
 * 하향링크(DL) 트래픽에 대해 실행되는 첫 번째 이벤트입니다. 
 */
void
StartApplicationDl (Ptr<MyModel> model) {
  model -> SendPacketDl ();
}

/*
 * Function은 단일 패킷을 생성하고 디바이스의 함수 전송을 직접 호출하여 목적지 주소로 패킷을 전송합니다.
 * 하향링크(DL) 트래픽
 */
void
MyModel::SendPacketDl () {
  // 패킷 생성
  Ptr<Packet> pkt = Create<Packet> (m_packetSize, m_periodicity, m_deadline);

  // 인터넷 헤더 추가
  Ipv4Header ipv4Header;
  ipv4Header.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
  pkt->AddHeader(ipv4Header);

  // 패킷 태그 추가
  EpsBearerTag tag (1,1);
  pkt->AddPacketTag (tag);

  m_device->Send (pkt, m_addr, Ipv4L3Protocol::PROT_NUMBER);
  NS_LOG_INFO ("하향링크(DL) 전송 중");

  // 전체 패킷 수 보다 보낸 패킷수가 작을 경우만 하향링크 스케줄링
  if (++m_packetsSent < m_nPackets) {
    ScheduleTxDl();
  }
}

/*
 * SendPacket은 다음 시간에 즉시 패킷을 생성합니다.
 */
void
MyModel::ScheduleTxDl () {
  if (m_running) {
    Time tNext = MilliSeconds(2);
    m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketDl, this);
  }
}

/*
 * 상향링크(UL) 트래픽에 대해 실행되는 첫 번째 이벤트입니다.
 */
void
StartApplicationUl (Ptr<MyModel> model) {
  model -> SendPacketUl ();
}

void
HandleTtiEvent () {
  //  이탈: active 풀에서 1~2개 랜덤 선택 StopApplication
  //  uint32_t leaveNum = std::rand() % 2 + 1;
  //  for (uint32_t i = 0; i < leaveNum && !active.empty(); ++i) {
  //     uint32_t pos = std::rand() % active.size();
  //     uint32_t idx = active[pos];
  //     std::swap(active[pos], active.back());
  //     active.pop_back();
  //     inactive.push_back(idx);
  //     NS_LOG_INFO (" UE 이탈 (활성 UE 수 : " << active.size() << ")");
  //     // 애플리케이션 중단
  //     uint32_t idx = gnbMacPtr->rntiToUeIndex[rnti];
  //     ueModels[idx]->StopModel();
  //  }
  
  // 합류: inactive 풀에서 1~2개 랜덤 선택해 StartApplication
  uint32_t joinNum = std::min<uint32_t>(std::rand() % 2 + 1, inactive.size());
  for (uint32_t i = 0; i < joinNum; ++i) {
    uint32_t idx = inactive.back();
    inactive.pop_back();
    active.push_back(idx);

    NS_LOG_INFO ("UE 합류 (활성 UE 수 : " << active.size() << ")");
    Simulator::ScheduleNow(&StartApplicationUl, ueModels[idx]);
  }

  // 50ms 마다 반복
  Simulator::Schedule(MilliSeconds(50), &HandleTtiEvent);
}

/*
 * Function은 단일 패킷을 생성하고 디바이스의 함수 전송을 직접 호출하여
 * 목적지 주소로 패킷을 전송합니다.
 * (UL TRAFFIC), 상향링크 트래픽
 */
void
MyModel::SendPacketUl () {
  // 패킷 생성
  Ptr<Packet> pkt = Create<Packet> (m_packetSize, m_periodicity, m_deadline);

  uint64_t creationTime = Simulator::Now().GetMicroSeconds(); // 패킷 생성 시간(μs)
  PacketCreationTimeTag creationTimeTag(creationTime);
  pkt->AddPacketTag(creationTimeTag);                         // 패킷에 태그를 통해서 생성 시간 삽입
  std::cout << "\n패킷 생성 시간:" << creationTime << "μs\n" << std::endl;

  PacketPriorityTag PriortyTag(m_priority);
  pkt->AddPacketTag(PriortyTag);                              // 패킷에 태그를 통해 우선순위 삽입

  PacketUeIdTag ueIdTag (m_ueIndex);
  pkt->AddPacketTag (ueIdTag);

  Ipv4Header ipv4Header;
  ipv4Header.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
  pkt->AddHeader(ipv4Header);

  m_device->Send (pkt, m_addr, Ipv4L3Protocol::PROT_NUMBER);

  if (!m_isConfigured) {
    // 최초 전송: Configuration 후에 다시 전송
    m_isConfigured = true;
    m_packetsSent = 1;
    ScheduleTxUl_Configuration();
    return;
  }
  else {
    ++m_packetsSent;
    if (m_nPackets == 0 || m_packetsSent < m_nPackets) // m_nPackets == 0 이면 (풀-버퍼), 아니면 설정된 개수
    {
      ScheduleTxUl (m_periodicity);
    }
  }
}

/*
 * SendPacket은 다음 시간에 즉시 패킷을 생성합니다.
 */
void MyModel::ScheduleTxUl (uint8_t period) {
  if (m_running) {
    Time tNext = MilliSeconds(period);
    m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketUl, this);
  }
}

void MyModel::ScheduleTxUl_Configuration (void) {
  uint8_t configurationTime = 60; // 60ms
  Time tNext = MilliSeconds(configurationTime);
  m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketUl, this);
}

/*
 * TraceSink, RxRlcPDU는 트레이스 싱크와 트레이스 소스(RxPDU)를 연결합니다.
 * UE를 gNB와 연결하거나 그 반대의 경우도 마찬가지입니다.
 */
void RxRlcPDU (std::string path, uint16_t rnti, uint8_t lcid, uint32_t bytes, uint64_t rlcDelay) {
  g_rxRxRlcPDUCallbackCalled = true;  // RLC 수신 성공 시 True 설정
  delay = Time::FromInteger(rlcDelay,Time::NS);
  m_ScenarioFile << "\n\n Data received at RLC layer at:"  << Simulator::Now () << std::endl;
  m_ScenarioFile << "\n rnti:" << rnti  << std::endl;
  m_ScenarioFile << "\n delay :" << rlcDelay << std::endl;
}

void RxPdcpPDU (std::string path, uint16_t rnti, uint8_t lcid, uint32_t bytes, uint64_t pdcpDelay) {
  g_rxPdcpCallbackCalled = true;  // PDCP 수신 성공 시 True 설정
}

void ConnectUlPdcpRlcTraces () {
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/UeMap/*/DataRadioBearerMap/*/LtePdcp/RxPDU", MakeCallback (&RxPdcpPDU));
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/UeMap/*/DataRadioBearerMap/*/LteRlc/RxPDU", MakeCallback (&RxRlcPDU));
  NS_LOG_INFO ("Received PDCP RLC UL");
}


/************************************************************ << main >> ************************************************************/
int
main (int argc, char *argv[]) {
  uint16_t numerologyBwp1 = 1;              // 뉴머롤로지 : 대역폭 부분에서 사용할 서브캐리어 간격 설정
  uint32_t packetSize = 10;                 // 패킷 크기 Byte
  double centralFrequencyBand1 = 3550e6;    // 주파수 3550 MHz
  double bandwidthBand1 = 20e6;             // 대역폭 20 MHz
  uint32_t init = 100000;                   // 초기 지연 시간 μs
  uint8_t period = uint8_t(10);             // 생성주기 ms
  uint32_t deadline = 10000000;             // 마감 시간 μs
  uint16_t TTI = 100;                       // Age Update TTI(µs)

  uint16_t gNbNum = 1;                      // 기지국 수
  uint16_t ueNumPergNb = 10;                // 단말 수
  uint16_t buildingNum = 5;                 // 빌딩 수

  bool enableUl = true;                     // 상향 트래픽
  uint32_t nPackets = 0;                    // 패킷 총 개수(0 이면 풀버퍼, 아니면 설정된 개수)
  Time sendPacketTime = Seconds(0);         // 패킷 전송 시작 전에 대기하는 시간, 패킷 전송 지연 시간
  uint8_t sch = 1;                          // 스케줄러 타입 (0: TDMA /1: OFDMA /2: Sym-OFDMA /3: RB-OFDMA)
                                            // Sym-OFDMA : 각 UE에 필요한 최소한의 OFDM 심볼을 할당
                                            // RB-OFDMA : 주어진 주파수 자원을 최대한 많은 UE가 공유
  uint8_t SchedulerChoice = 1;              // 스케줄러 선택 (0: RR / 1: PF / 2: AG(AgeGreedy) / 3: PG(PriorityGreedy))

  u_int32_t seed = 42;
  // u_int32_t seed = static_cast<u_int32_t>(time(nullptr));
  std::srand(seed);                         // 시드를 현재 시간으로 설정 42(*), 537(V), 1858(V), 3022(V), 3108(V), 4472(V), 4485(V), 5854(V), 8623(V), 9391(V)
  delay = MicroSeconds(10);                 // 트래픽에 적용할 지연 시간(딜레이)을 설정
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (80)); // 최대 N개의 UE까지 등록가능 Allowed values: 2 5 10 20 40 80 160 320

  CommandLine cmd;
  cmd.AddValue ("numerologyBwp1", "The numerology to be used in bandwidth part 1", numerologyBwp1);
  cmd.AddValue ("centralFrequencyBand1", "The system frequency to be used in band 1", centralFrequencyBand1);
  cmd.AddValue ("bandwidthBand1", "The system bandwidth to be used in band 1", bandwidthBand1);
  cmd.AddValue ("packetSize", "packet size in bytes", packetSize);
  cmd.AddValue ("enableUl", "Enable Uplink", enableUl);
  cmd.AddValue ("scheduler", "Scheduler", sch);
  cmd.Parse (argc, argv);

  /*********************************************************< Age가 넘어가는 경로의 gNB, Scheduler NS3 LOG INFO >******************************************************/
  
  LogComponentEnable("ConfiguredGrant", LOG_INFO);
  // LogComponentEnable("NrGnbMac", LOG_INFO);
  LogComponentEnable("NrUeMac", LOG_INFO);
  //LogComponentEnable("NrMacSchedulerNs3", LOG_INFO);
  // LogComponentEnable("NrMacSchedulerTdma", LOG_INFO);
  LogComponentEnable("NrMacSchedulerOfdma", LOG_INFO);

  /*******************************************************************************************************************************************************************/

  std::vector<uint32_t> v_init(ueNumPergNb);        // 각 UE가 첫 패킷을 전송하기 전의 초기 지연 시간
  std::vector<uint32_t> v_period(ueNumPergNb);      // 주기적인 전송 간격을 설정하여 각 UE가 상향 링크 패킷을 주기적으로 전송
  std::vector<uint32_t> v_deadline(ueNumPergNb);    // 각 패킷이 전송되어야 하는 마감 시간을 설정하여 패킷이 전송되는 데 필요한 최대 시간
  std::vector<uint32_t> v_packet(ueNumPergNb);      // 각 패킷의 크기를 바이트 단위로 설정

  v_init = std::vector<uint32_t> (ueNumPergNb,{init});          // μs
  v_deadline = std::vector<uint32_t> (ueNumPergNb,{deadline});  // μs
  v_packet = std::vector<uint32_t> (ueNumPergNb,{packetSize});  // Byte
  v_period = std::vector<uint32_t> (ueNumPergNb,{period});      // ms

  m_ScenarioFile.open("Scenario.txt", std::ofstream::out | std::ofstream::trunc);

  std::ostream_iterator<std::uint32_t> output_iterator(m_ScenarioFile, "\n");
  m_ScenarioFile <<  "Nº UE" << "\t" << "Init" << "\t" << "Latency" << "\t" << "Periodicity" << std::endl;
  m_ScenarioFile <<ueNumPergNb << std::endl;
  std::copy(v_init.begin(), v_init.end(),  output_iterator);
  m_ScenarioFile << std::endl;
  std::copy(v_deadline.begin(), v_deadline.end(), output_iterator);
  m_ScenarioFile << std::endl;
  std::copy(v_period.begin(), v_period.end(), output_iterator);
  m_ScenarioFile << std::endl;

  int64_t randomStream = 1;
/************************************************** << 네트워크 토폴리지 구역 >> ***********************************************
 * gNB와 UE를 설정 할 수 있다. 자세한 사항은 GridScenarioHelper 문서를 참조하여 노드가 어떻게 분배되는지 확인하길 바랍니다.
 * 내가 설정한 네트워크 토폴리지는 중앙에 gNB 1개가 생성되고, 12개의 UE가 무작위 위치에서 생성되고 1 ~ 14m/s로 움직인다.
 *******************************************************************************************************************************/
  NodeContainer gNBNodes;
  NodeContainer ueNodes;
  BuildingContainer buildings;

  gNBNodes.Create(gNbNum);
  ueNodes.Create(ueNumPergNb * gNbNum);
  buildings.Create(buildingNum);

  MobilityHelper mobility;

  // 기지국(BS, gNB) 위치 설정
  Ptr<ListPositionAllocator> PositionAlloc = CreateObject<ListPositionAllocator> ();
  PositionAlloc -> Add (Vector (0.0, 0.0, 1.5));  // gNB 좌표 (X,Y,Z)
  mobility.SetPositionAllocator(PositionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(gNBNodes);

  // UE 위치 및 이동성 설정
  mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=50.0]"),
                                "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=50.0]"));
  mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                            "Bounds", RectangleValue(Rectangle(0, 200, 0, 200)),
                            "Speed", StringValue("ns3::UniformRandomVariable[Min=1.0|Max=14.0]"));
  mobility.Install(ueNodes);

  BuildingsHelper::Install(gNBNodes);
  BuildingsHelper::Install(ueNodes);

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();
  nrHelper->SetBeamformingHelper (idealBeamformingHelper);

  /*************************************** 스케줄러 유형 선택 ******************************************
  * configured grant <-> grant based
  * true -> configured grant
  * false -> grant based
  ****************************************************************************************************/
  bool scheduler_CG = true;
  uint8_t configurationTime = 60;

  nrHelper->SetUeMacAttribute ("CG", BooleanValue (scheduler_CG));
  nrHelper->SetUePhyAttribute ("CG", BooleanValue (scheduler_CG));
  nrHelper->SetGnbMacAttribute ("CG", BooleanValue (scheduler_CG));
  nrHelper->SetGnbPhyAttribute ("CG", BooleanValue (scheduler_CG));

  if (scheduler_CG) {
    // 구성 시간
    // UE, 이동 단말
    nrHelper->SetUeMacAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    nrHelper->SetUePhyAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    // gNB, 기지국
    nrHelper->SetGnbMacAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    nrHelper->SetGnbPhyAttribute ("ConfigurationTime", UintegerValue (configurationTime));
  }
  else {
    nrHelper->SetSchedulerAttribute ("CG", BooleanValue (scheduler_CG));
  }

  nrHelper->SetEpcHelper (epcHelper);

  // SRS 과정 비활성화
  nrHelper->SetSchedulerAttribute ("SrsSymbols", UintegerValue (0));

  // 원하는 유연한 패턴을 추가합니다.(필요한 하향링크(DL) 데이터 symbols (기본값 0))
  nrHelper->SetSchedulerAttribute ("DlDataSymbolsFpattern", UintegerValue (0)); //symStart - 1

  // HARQ 재전송 활성화 또는 비활성화
  nrHelper->SetSchedulerAttribute ("EnableHarqReTx", BooleanValue (false));   // 기본 값 false
  Config::SetDefault ("ns3::NrHelper::HarqEnabled", BooleanValue (false));  // 기본 값 false 

  // 스케줄러 선택
  if (sch != 0) {
    switch (SchedulerChoice) {
    case 0: // Round Robin (RR)
        nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
        LogComponentEnable("NrMacSchedulerOfdmaRR", LOG_INFO);
        std::cout << "\n스케줄러 종류 : Round Robin (RR)\n" << std::endl;
      break;
    case 1: // Proportional Fair (PF)
        nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaPF::GetTypeId ());
        LogComponentEnable("NrMacSchedulerOfdmaPF", LOG_INFO);
        std::cout << "\n스케줄러 종류: Proportional Fair (PF)\n" << std::endl;
      break;
    case 2: // Age Greedy (AG)
        nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaAG::GetTypeId ());
        LogComponentEnable("NrMacSchedulerOfdmaAG", LOG_INFO);
        std::cout << "\n스케줄러 종류 : Age Greedy (AG)\n" << std::endl;
      break;
    case 3: // Priority Greedy (PG)
        nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaPG::GetTypeId ());
        LogComponentEnable("NrMacSchedulerOfdmaPG", LOG_INFO);
        std::cout << "\n스케줄러 종류 : Priority Greedy (PG)\n" << std::endl;
      break;
    default: // Round Robin (RR)
        nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
        LogComponentEnable("NrMacSchedulerOfdmaRR", LOG_INFO);
        std::cout << "\n기본 스케줄러 : Round Robin (RR)\n" << std::endl;
      break;
    }
    nrHelper->SetSchedulerAttribute ("schOFDMA", UintegerValue (sch)); // sch
                                                                        // 0 == TDMA
                                                                        // 1 == 5GL-OFDMA
                                                                        // 2 == Sym-OFDMA
                                                                        // 3 == RB-OFDMA
  }

  // 하나의 대역폭 부분(BWP)으로 하나의 CC를 포함하는 하나의 운영 대역을 만듭니다.
  BandwidthPartInfoPtrVector allBwps;
  CcBwpCreator ccBwpCreator;
  const uint8_t numCcPerBand = 1;
  
  // 로스(LOS) : 중심 주파수, 대역폭, 컴포넌트 수
  CcBwpCreator::SimpleOperationBandConf bandConf1 (centralFrequencyBand1, bandwidthBand1, numCcPerBand, BandwidthPartInfo::UMi_Buildings);

  // 생성된 구성을 사용하여 작업 대역을 만들 때입니다.
  OperationBandInfo band1 = ccBwpCreator.CreateOperationBandContiguousCc (bandConf1);

  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",TimeValue (MilliSeconds(0)));
  nrHelper->SetSchedulerAttribute ("FixedMcsDl", BooleanValue (true));
  nrHelper->SetSchedulerAttribute ("StartingMcsDl", UintegerValue(4));

  // CG일 경우 참이어야 합니다.
  nrHelper->SetSchedulerAttribute ("FixedMcsUl", BooleanValue (true));
  nrHelper->SetSchedulerAttribute ("StartingMcsUl", UintegerValue(12));

  nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod", TimeValue (MilliSeconds (0)));
  nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (true)); //false

  // 오류 모델(Error Model): 동일한 스펙트럼 오류 모델을 가진 UE와 gNB.
  // ns3::NrEesmIrT2 (256QAM), ns3::NrEesmIrT1 (64QAM) 이 두 가지는 강력하지만, 처리량은 적습니다.
  std::string errorModel = "ns3::NrEesmIrT1"; 
  nrHelper->SetUlErrorModel (errorModel);
  nrHelper->SetDlErrorModel (errorModel);

  // 하향링크(DL)과 상향링크(UL) AMC 모두 동일한 모델이 적용됩니다.
  nrHelper->SetGnbDlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel)); // NrAmc::ShannonModel 또는 NrAmc::ErrorModel
  nrHelper->SetGnbUlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel)); // NrAmc::ShannonModel 또는 NrAmc::ErrorModel
  
  // 채널 환경에 따른 패킷 드롭(전송 실패) (Signal Loss / Fading)
  bool fadingEnabled = true; 
  auto bandMask = NrHelper::INIT_PROPAGATION | NrHelper::INIT_CHANNEL;
  if (fadingEnabled){
    bandMask |= NrHelper::INIT_FADING;
  }

  nrHelper->InitializeOperationBand (&band1, bandMask);
  allBwps = CcBwpCreator::GetAllBwps ({band1});

  // 빔포밍(Beamforming) 방법
  idealBeamformingHelper->SetAttribute ("BeamformingMethod", TypeIdValue (QuasiOmniDirectPathBeamforming::GetTypeId ()));

  // 모든 UE들을 위한 안테나
  nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (2));
  nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (4));
  nrHelper->SetUeAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));

  // 모든 gNb들을 위한 안테나
  nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (4));
  nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (4));
  nrHelper->SetGnbAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));

  // gNB와 UE에 대한 NetDevices 설치 및 포인터 가져오기
  NetDeviceContainer enbNetDev = nrHelper->InstallGnbDevice (gNBNodes, allBwps);
  NetDeviceContainer ueNetDev = nrHelper->InstallUeDevice (ueNodes, allBwps);
  ueDevs.resize(ueNumPergNb);
  for (uint32_t i = 0; i < ueNumPergNb; ++i) {
    ueDevs[i] = DynamicCast<NrUeNetDevice>(ueNetDev.Get(i));
  }

  randomStream += nrHelper->AssignStreams (enbNetDev, randomStream);
  randomStream += nrHelper->AssignStreams (ueNetDev, randomStream);

  // netdevice (enbNetDev.Get (0)) and bandwidth part (0)의 속성을 설정합니다.
  nrHelper->GetGnbPhy (enbNetDev.Get (0), 0)->SetAttribute ("Numerology", UintegerValue (numerologyBwp1));

  for (auto it = enbNetDev.Begin (); it != enbNetDev.End (); ++it){
    DynamicCast<NrGnbNetDevice> (*it)->UpdateConfig ();
  }

  for (auto it = ueNetDev.Begin (); it != ueNetDev.End (); ++it){
    DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();
  }

  // 인터넷 스택 설치
  InternetStackHelper internet;
  internet.Install (ueNodes);
  
  // IP 주소 전달, EPC(4G core) 모듈을 통해 UE에 IP 주소 할당
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueNetDev));

  // 가장 가까운 eNB에 UE 연결하기
  nrHelper->AttachToClosestEnb (ueNetDev, enbNetDev);
  nrHelper->EnableTraces();

  // gNB의 MAC 객체에 대한 포인터를 얻습니다.
  gnbMacPtr = DynamicCast<NrGnbMac>(enbNetDev.Get(0)->GetObject<NrGnbNetDevice>()->GetMac(0));

  // 상향링크(UL) 트래픽
  ueModels.reserve(ueNumPergNb);
  for (uint32_t i = 0; i < ueNumPergNb; ++i) {
    Ptr<MyModel> UE = CreateObject<MyModel>();
    uint32_t priority = (std::rand() % 8 + 1);                   // 우선순위를 무작위로 1 ~ 8 결정
    UE->SetPriority (priority);
    UE->Setup(ueNetDev.Get (i), enbNetDev.Get (0)->GetAddress (), packetSize, nPackets, DataRate ("1Mbps"), period, deadline);
    ueModels.push_back (UE);
    if (i < (ueNumPergNb/2)) {
      // 활성 풀에 적용
      active.push_back(i);
      Simulator::Schedule (MicroSeconds(v_init[i]), &StartApplicationUl, UE);
    }
    else {
      // 비활성 풀에 적용
      inactive.push_back(i);
    }
  }
  Simulator::Schedule (MilliSeconds (200), &HandleTtiEvent);

  gnbMacPtr->StartPeriodicAgeUpdate(TTI, deadline);  // Age 계산 주기적 호출 시작 추가

  Simulator::Schedule (Seconds (0.16), &ConnectUlPdcpRlcTraces);
  Simulator::Stop (Seconds (30.0));

  Simulator::Schedule (Seconds (30.0) - NanoSeconds (2), &NrGnbMac::PrintAverageThroughput, gnbMacPtr);
  Simulator::Schedule (Seconds (30.0) - NanoSeconds (2), &NrGnbMac::PrintAverageAoI, gnbMacPtr);
  Simulator::Run ();

  std::cout<<"\n 종료. "<<std::endl;

  if (g_rxPdcpCallbackCalled && g_rxRxRlcPDUCallbackCalled){
    return EXIT_SUCCESS;  // RLC와 PDCP 계층에서 모두 성공적으로 수신 → 성공 상태 반환
  }
  else{
    return EXIT_FAILURE;  // RLC나 PDCP 계층 중 하나라도 수신 실패 시 → 실패 상태 반환
  }

  Simulator::Destroy ();
}