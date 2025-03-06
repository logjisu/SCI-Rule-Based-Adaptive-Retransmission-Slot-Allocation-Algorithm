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
 * 설명: 이 코드는 UE에서 gNB로 100 패킷을 전송합니다. 동적 또는 Configured-Grant(CG) 스케줄러와
 * 함께 작동합니다. (두 스케줄러 모두 동시에 작동할 수 없습니다.)
 *
 * CG의 경우 구성 시간이 선택됩니다. 이 시간에 UE는 요구 사항을 gNB로 전송합니다.
 * gNB는 각 UE에 대한 CG를 생성합니다.
 *
 * OFDMA 또는 TDMA 액세스 모드를 사용할 수 있습니다.
 * 하지만, OFDMA 액세스 모드에 사용할 두 가지 새로운 스케줄링 정책이 포함되어 있습니다.
 *
 * 이 코드는 "cttc-3gpp-channel-simple-ran.cc"(5G-LENA) 코드를 기반으로 작성되었습니다.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-module.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/eps-bearer-tag.h"
#include "ns3/log.h"
#include "ns3/antenna-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/a-packet-tags.h"
#include <cstdlib>  // 랜덤 값 생성에 필요
#include <ctime>    // 시간 기반 시드 설정에 필요

using namespace ns3;

/*
 * "ConfiguredGrant" 구성 요소를 활성화하여 파일의 로그를 활성화합니다.
 * 아래와 같은 방식으로:
 * $ export NS_LOG="ConfiguredGrant=level_info|prefix_func|prefix_time"
 */
NS_LOG_COMPONENT_DEFINE ("ConfiguredGrant");

//export NS_LOG="ConfiguredGrant=level_info|prefix_func|prefix_time:NrMacSchedulerOfdmaPF=level_info|prefix_func|prefix_time"

static bool g_rxPdcpCallbackCalled = false;
static bool g_rxRxRlcPDUCallbackCalled = false;

/*
 * 전역 변수
 */
Time g_txPeriod = Seconds(0.1);
Time delay;
std::fstream m_ScenarioFile;

std::vector<uint64_t> packetCreationTimes;  // 각 패킷 생성 시간을 저장할 벡터

/*
 * MyModel 클래스
 * 이 클래스에는 UE에서 gNB로 패킷을 전송하는 이벤트를 생성하는 기능이 포함되어 있습니다.
*/

class MyModel : public Application{
public:
  MyModel ();
  virtual ~MyModel();

  void Setup (Ptr<NetDevice> device, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, uint8_t period, uint32_t deadline);

  // 하향링크(DL)
  void SendPacketDl ();
  void ScheduleTxDl ();

  // 상향링크(UL)
  void SendPacketUl ();
  void ScheduleTxUl (uint8_t period);
  void ScheduleTxUl_Configuration();

private:
  Ptr<NetDevice>  m_device;
  Address         m_addr;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
  uint8_t         m_periodicity;
  uint32_t        m_deadline;
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


void MyModel::Setup (Ptr<NetDevice> device, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, uint8_t period, uint32_t deadline){
  m_device = device;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
  m_running = true;
  m_packetsSent = 0;
  m_periodicity = period;
  m_deadline = deadline;
}

/*
 * 하향링크(DL) 트래픽에 대해 실행되는 첫 번째 이벤트입니다. 
 */
void StartApplicationDl (Ptr<MyModel> model){
  model -> SendPacketDl ();
}
/*
 * Function은 단일 패킷을 생성하고 디바이스의 함수 전송을 직접 호출하여
 * 목적지 주소로 패킷을 전송합니다.
 * (DL TRAFFIC), 하향링크 트래픽
 */
void MyModel::SendPacketDl (){
  // 패킷 생성
  Ptr<Packet> pkt = Create<Packet> (m_packetSize,m_periodicity,m_deadline);

  Ipv4Header ipv4Header;
  ipv4Header.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
  pkt->AddHeader(ipv4Header);

  EpsBearerTag tag (1,1);
  pkt->AddPacketTag (tag);

  m_device->Send (pkt, m_addr, Ipv4L3Protocol::PROT_NUMBER);
  NS_LOG_INFO ("Sending DL");

  if (++m_packetsSent < m_nPackets){
    ScheduleTxDl();
  }
}
/*
 * SendPacket은 다음 시간에 즉시 패킷을 생성합니다.
 */
void MyModel::ScheduleTxDl (){
  if (m_running){
    Time tNext = MilliSeconds(2);
    m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketDl, this);
  }
}

/*
 * 상향링크(UL) 트래픽에 대해 실행되는 첫 번째 이벤트입니다.
 */
void StartApplicationUl (Ptr<MyModel> model){
  model -> SendPacketUl ();
}
/*
 * Function은 단일 패킷을 생성하고 디바이스의 함수 전송을 직접 호출하여
 * 목적지 주소로 패킷을 전송합니다.
 * (UL TRAFFIC), 상향링크 트래픽
 */
void MyModel::SendPacketUl ()
{
  Ptr<Packet> pkt = Create<Packet> (m_packetSize,m_periodicity,m_deadline);

  // 패킷에 생성 시간 태그 추가
  uint64_t creationTime = Simulator::Now().GetMicroSeconds();
  PacketCreationTimeTag creationTimeTag(creationTime);
  pkt->AddPacketTag(creationTimeTag);
  std::cout << "\n 패킷 생성 시간:" << creationTime << "ms" << std::endl;

  // 무작위로 긴급한 패킷인지 여부를 결정
  uint32_t Urgent = (std::rand() % 8 + 1);  // 긴급도를 1 ~ 8로 결정
  PacketUrgencyTag urgencyTag(Urgent);
  pkt->AddPacketTag(urgencyTag);

  Ipv4Header ipv4Header;
  ipv4Header.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
  pkt->AddHeader(ipv4Header);

  m_device->Send (pkt, m_addr, Ipv4L3Protocol::PROT_NUMBER);
  NS_LOG_INFO ("Sending UL");

  if (m_packetsSent==0){
    ScheduleTxUl_Configuration();
    m_packetsSent = 1;
  }
  else if (++m_packetsSent < m_nPackets){
    ScheduleTxUl (m_periodicity);
  }
}
/*
 * SendPacket은 다음 시간에 즉시 패킷을 생성합니다.
 */

void MyModel::ScheduleTxUl (uint8_t period){
  if (m_running){
    Time tNext = MilliSeconds(period);
    m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketUl, this);
  }
}

void MyModel::ScheduleTxUl_Configuration (void){
  uint8_t configurationTime = 60;
  Time tNext = MilliSeconds(configurationTime);
  m_sendEvent = Simulator::Schedule (tNext, &MyModel::SendPacketUl, this);
}

/*
 * TraceSink, RxRlcPDU는 트레이스 싱크와 트레이스 소스(RxPDU)를 연결합니다.***********************************************************************************************************************
 * UE를 gNB와 연결하거나 그 반대의 경우도 마찬가지입니다.
 */
void RxRlcPDU (std::string path, uint16_t rnti, uint8_t lcid, uint32_t bytes, uint64_t rlcDelay){
  g_rxRxRlcPDUCallbackCalled = true;
  delay = Time::FromInteger(rlcDelay,Time::NS);

  //std::cout<<"\n rlcDelay in NS (Time):"<< delay<<std::endl;
  //std::cout<<"\n\n Data received at RLC layer at:"<<Simulator::Now()<<std::endl;

  m_ScenarioFile << "\n\n Data received at RLC layer at:"  << Simulator::Now () << std::endl;
  m_ScenarioFile << "\n rnti:" << rnti  << std::endl;
  m_ScenarioFile << "\n delay :" << rlcDelay << std::endl;
}

void RxPdcpPDU (std::string path, uint16_t rnti, uint8_t lcid, uint32_t bytes, uint64_t pdcpDelay){
  //std::cout << "\n Packet PDCP delay:" << pdcpDelay << "\n";
  g_rxPdcpCallbackCalled = true;
}

void ConnectUlPdcpRlcTraces (){
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/UeMap/*/DataRadioBearerMap/*/LtePdcp/RxPDU", MakeCallback (&RxPdcpPDU));
  Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/UeMap/*/DataRadioBearerMap/*/LteRlc/RxPDU", MakeCallback (&RxRlcPDU));
  NS_LOG_INFO ("Received PDCP RLC UL");
}


/************************************************************ << main >> ************************************************************/
int main (int argc, char *argv[]){
  uint16_t numerologyBwp1 = 2;              // 대역폭 부분에서 사용할 서브캐리어 간격을 설정
  uint32_t packetSize = 10;                 // 패킷 크기 Byte
  double centralFrequencyBand1 = 3550e6;    // 주파수 3550 MHz
  double bandwidthBand1 = 20e6;             // 대역폭 20 MHz
  uint8_t period = uint8_t(10);              // 전송주기 ms
  
  uint16_t gNbNum = 1;                      // 기지국 수
  uint16_t ueNumPergNb = 37;                // 단말 수

  bool enableUl = true;                     // 상향 트래픽 추적
  uint32_t nPackets = 1000000;              // 패킷 총 개수
  Time sendPacketTime = Seconds(0);         // 패킷 전송 시작 전에 대기하는 시간, 패킷 전송 지연 시간
  uint8_t sch = 1;                          // 스케줄러 타입 (0: TDMA /1: OFDMA /2: Sym-OFDMA /3: RB-OFDMA)
                                            // Sym-OFDMA : 각 UE에 필요한 최소한의 OFDM 심볼을 할당
                                            // RB-OFDMA : 주어진 주파수 자원을 최대한 많은 UE가 공유
  uint8_t SchedulerChoice = 2;              // 스케줄러 선택 (0: RR / 1: PF / 2: AG(AgeGreedy))

  u_int32_t seed = 2024;
  std::srand(seed);                         // 시드를 현재 시간으로 설정 42(*), 537(V), 1858(V), 3022(V), 3108(V), 4472(V), 4485(V), 5854(V), 8623(V), 9391(V)
  delay = MicroSeconds(10);                 // 트래픽에 적용할 지연 시간(딜레이)을 설정

  CommandLine cmd;
  cmd.AddValue ("numerologyBwp1", "The numerology to be used in bandwidth part 1", numerologyBwp1);
  cmd.AddValue ("centralFrequencyBand1", "The system frequency to be used in band 1", centralFrequencyBand1);
  cmd.AddValue ("bandwidthBand1", "The system bandwidth to be used in band 1", bandwidthBand1);
  cmd.AddValue ("packetSize", "packet size in bytes", packetSize);
  cmd.AddValue ("enableUl", "Enable Uplink", enableUl);
  cmd.AddValue ("scheduler", "Scheduler", sch);
  cmd.Parse (argc, argv);

  /*********************************************************< Age가 넘어가는 경로의 gNB, Scheduler NS3 LOG INFO >******************************************************/
  
  //LogComponentEnable("ConfiguredGrant", LOG_INFO);
  LogComponentEnable("NrGnbMac", LOG_INFO);
  //LogComponentEnable("NrMacSchedulerNs3", LOG_INFO);
  //LogComponentEnable("NrMacSchedulerTdma", LOG_INFO);
  //LogComponentEnable("NrMacSchedulerOfdma", LOG_INFO);

  /*******************************************************************************************************************************************************************/

  std::vector<uint32_t> v_init(ueNumPergNb);        // 각 UE가 첫 패킷을 전송하기 전의 초기 지연 시간
  std::vector<uint32_t> v_period(ueNumPergNb);      // 주기적인 전송 간격을 설정하여 각 UE가 상향 링크 패킷을 주기적으로 전송
  std::vector<uint32_t> v_deadline(ueNumPergNb);    // 각 패킷이 전송되어야 하는 마감 시간을 설정하여 패킷이 전송되는 데 필요한 최대 시간
  std::vector<uint32_t> v_packet(ueNumPergNb);      // 각 패킷의 크기를 바이트 단위로 설정

  //std::cout << "\n Init values: " << '\n';
  v_init = std::vector<uint32_t> (ueNumPergNb,{100000});           // μs
  // for (int val : v_init)
  //         std::cout << val << std::endl;

  //std::cout << "Deadline values: " << '\n';
  v_deadline = std::vector<uint32_t> (ueNumPergNb,{10000000});  // μs
  // for (int val : v_deadline)
  //          std::cout << val << std::endl;

  std::cout << "Packet values: " << '\n';
  v_packet = std::vector<uint32_t> (ueNumPergNb,{packetSize});
  // for (int val : v_packet)
          // std::cout << val << std::endl;

  //std::cout << "Period values: " << '\n';
  v_period = std::vector<uint32_t> (ueNumPergNb,{period});
  // for (int val : v_period)
  //         std::cout << val << "\t";


  m_ScenarioFile.open("Scenario.txt", std::ofstream::out | std::ofstream::trunc);

  std::ostream_iterator<std::uint32_t> output_iterator(m_ScenarioFile, "\n");
  m_ScenarioFile <<  "Nº UE" << "\t" << "Init" << "\t" <<
                      "Latency" << "\t" << "Periodicity" << std::endl;

  m_ScenarioFile <<ueNumPergNb << std::endl;
  std::copy(v_init.begin(), v_init.end(),  output_iterator);
  m_ScenarioFile << std::endl;
  std::copy(v_deadline.begin(), v_deadline.end(), output_iterator);
  m_ScenarioFile << std::endl;
  std::copy(v_period.begin(), v_period.end(), output_iterator);
  m_ScenarioFile << std::endl;

  int64_t randomStream = 1;
/************************************************** << 네트워크 토폴리지 구역 >> **************************************************
 * gNB와 UE를 설정 할 수 있다. 자세한 사항은 GridScenarioHelper 문서를 참조하여 노드가 어떻게 분배되는지 확인하길 바랍니다.
 * 내가 설정한 네트워크 토폴리지는 중앙에 gNB 1개가 생성되고, 12개의 UE가 무작위 위치에서 생성되고 1 ~ 14m/s로 움직인다.
 *******************************************************************************************************************************/
  NodeContainer gNBNodes;
  NodeContainer ueNodes;

  gNBNodes.Create(gNbNum);
  ueNodes.Create(ueNumPergNb * gNbNum);

  MobilityHelper mobility;

  // 기지국(BS, gNB) 위치 설정
  Ptr<ListPositionAllocator> PositionAlloc = CreateObject<ListPositionAllocator> ();
  PositionAlloc -> Add (Vector (0.0, 0.0, 1.5));  // gNB 좌표 (X,Y,Z)
  mobility.SetPositionAllocator(PositionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(gNBNodes);

  // UE 위치 및 이동성 설정
  mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=10.0]"),
                                "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=10.0]"));
  mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                            "Bounds", RectangleValue(Rectangle(0, 100, 0, 100)),
                            "Speed", StringValue("ns3::UniformRandomVariable[Min=1.0|Max=14.0]"));
  mobility.Install(ueNodes);


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

  if (scheduler_CG){
    // 구성 시간
    // UE, 이동 단말
    nrHelper->SetUeMacAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    nrHelper->SetUePhyAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    // gNB, 기지국
    nrHelper->SetGnbMacAttribute ("ConfigurationTime", UintegerValue (configurationTime));
    nrHelper->SetGnbPhyAttribute ("ConfigurationTime", UintegerValue (configurationTime));
  }
  else{
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
    switch (SchedulerChoice){
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

  CcBwpCreator::SimpleOperationBandConf bandConf1 (centralFrequencyBand1, bandwidthBand1,
                                                        numCcPerBand, BandwidthPartInfo::InH_OfficeOpen_nLoS);


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
  // ns3::NrEesmIrT2 (256QAM), ns3::NrEesmIrT1 (64QAM) 이 두 가지는 강력하지만 처리량은 적습니다.
  std::string errorModel = "ns3::NrEesmIrT1"; 
  nrHelper->SetUlErrorModel (errorModel);
  nrHelper->SetDlErrorModel (errorModel);

  // 하향링크(DL)과 상향링크(UL) AMC 모두 동일한 모델이 적용됩니다.
  nrHelper->SetGnbDlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel)); // NrAmc::ShannonModel 또는 NrAmc::ErrorModel
  nrHelper->SetGnbUlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel)); // NrAmc::ShannonModel 또는 NrAmc::ErrorModel

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

  // NetDevices 설치 및 포인터 가져오기
  NetDeviceContainer enbNetDev = nrHelper->InstallGnbDevice (gNBNodes, allBwps);
  NetDeviceContainer ueNetDev = nrHelper->InstallUeDevice (ueNodes, allBwps);
  // NetDeviceContainer enbNetDev = nrHelper->InstallGnbDevice (gridScenario.GetBaseStations (), allBwps);
  // NetDeviceContainer ueNetDev = nrHelper->InstallUeDevice (gridScenario.GetUserTerminals (), allBwps);

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
  // internet.Install (gridScenario.GetUserTerminals ());
  internet.Install (ueNodes);
  
  // IP 주소 전달
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueNetDev));

  // 상향링크(UL) 트래픽
  std::vector <Ptr<MyModel>> v_modelUl;
  v_modelUl = std::vector<Ptr<MyModel>> (ueNumPergNb,{0});
  for (uint8_t ii=0; ii<ueNumPergNb; ++ii){
    Ptr<MyModel> modelUl = CreateObject<MyModel> ();
    modelUl -> Setup(ueNetDev.Get(ii), enbNetDev.Get(0)->GetAddress(), v_packet[ii], nPackets, DataRate("1Mbps"),v_period[ii], v_deadline[ii]);
    v_modelUl[ii] = modelUl;
    Simulator::Schedule(MicroSeconds(v_init[ii]), &StartApplicationUl, v_modelUl[ii]);
  }

  // 하향링크(DL) 트래픽
  //Ptr<MyModel> modelDl = CreateObject<MyModel> ();
  //modelDl -> Setup(enbNetDev.Get(0), ueNetDev.Get(0)->GetAddress(), 10, nPackets, DataRate("1Mbps"),20, uint32_t(100000));
  //Simulator::Schedule(MicroSeconds(0.099625), &StartApplicationDl, modelDl);


  // 가장 가까운 eNB에 UE 연결하기
  nrHelper->AttachToClosestEnb (ueNetDev, enbNetDev);
  nrHelper->EnableTraces();

  Simulator::Schedule (Seconds (0.16), &ConnectUlPdcpRlcTraces);
  Simulator::Stop (Seconds (10.0));

  // gNB의 MAC 객체에 대한 포인터를 얻습니다.
  Ptr<NrGnbMac> gnbMac = DynamicCast<NrGnbMac> (enbNetDev.Get (0)->GetObject<NrGnbNetDevice> ()->GetMac (0));

  Simulator::Schedule (Seconds (10.0) - NanoSeconds (2), &NrGnbMac::PrintAverageThroughput, gnbMac);
  Simulator::Run ();

  std::cout<<"\n FIN. "<<std::endl;

  if (g_rxPdcpCallbackCalled && g_rxRxRlcPDUCallbackCalled){
    return EXIT_SUCCESS;
  }
  else{
    return EXIT_FAILURE;
  }

  Simulator::Destroy ();
}