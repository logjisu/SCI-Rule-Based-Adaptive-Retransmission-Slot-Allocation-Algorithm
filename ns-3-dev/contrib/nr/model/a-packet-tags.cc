/* a-packet-tags.cc */

#include "a-packet-tags.h"

// 패킷 생성 시간
PacketCreationTimeTag::PacketCreationTimeTag() : m_creationTime(0) {}
PacketCreationTimeTag::PacketCreationTimeTag(uint64_t time) : m_creationTime(time) {}

void
PacketCreationTimeTag::SetCreationTime(uint64_t time){
  m_creationTime = time;
}

uint64_t
PacketCreationTimeTag::GetCreationTime() const{
  return m_creationTime;
}

void
PacketCreationTimeTag::Serialize(TagBuffer i) const{
  i.WriteU64(m_creationTime);
}

void
PacketCreationTimeTag::Deserialize(TagBuffer i){
  m_creationTime = i.ReadU64();
}

uint32_t
PacketCreationTimeTag::GetSerializedSize() const{
  return sizeof(uint64_t);
}

void
PacketCreationTimeTag::Print(std::ostream &os) const{
  os << "CreationTime=" << m_creationTime;
}

TypeId
PacketCreationTimeTag::GetTypeId(){
  static TypeId tid = TypeId("PacketCreationTimeTag")
      .SetParent<Tag>()
      .AddConstructor<PacketCreationTimeTag>();
  return tid;
}

TypeId
PacketCreationTimeTag::GetInstanceTypeId() const{
  return GetTypeId();
}

// 단말기(UE) ID
PacketUeIdTag::PacketUeIdTag() : m_ueId(0) {}
PacketUeIdTag::PacketUeIdTag(uint32_t ueId) : m_ueId(ueId) {}

void
PacketUeIdTag::SetUeId(uint32_t ueId){
  m_ueId = ueId;
}

uint32_t
PacketUeIdTag::GetUeId() const{
  return m_ueId;
}

void
PacketUeIdTag::Serialize(TagBuffer i) const{
  i.WriteU32(m_ueId);
}

void
PacketUeIdTag::Deserialize(TagBuffer i){
  m_ueId = i.ReadU32();
}

uint32_t
PacketUeIdTag::GetSerializedSize() const{
  return sizeof(uint32_t);
}

void
PacketUeIdTag::Print(std::ostream &os) const{
  os << "UeId=" << m_ueId;
}

TypeId
PacketUeIdTag::GetTypeId(){
  static TypeId tid = TypeId("PacketUeIdTag")
      .SetParent<Tag>()
      .AddConstructor<PacketUeIdTag>();
  return tid;
}

TypeId
PacketUeIdTag::GetInstanceTypeId() const{
  return GetTypeId();
}

// 패킷 우선순위 부여
PacketPriorityTag::PacketPriorityTag() : m_Priority(0) {}
PacketPriorityTag::PacketPriorityTag(uint32_t Priority) : m_Priority(Priority) {}

void
PacketPriorityTag::SetPriority(uint32_t Priority){
  m_Priority = Priority;
}

uint32_t
PacketPriorityTag::GetPriority() const{
  return m_Priority;
}

void
PacketPriorityTag::Serialize(TagBuffer i) const{
  i.WriteU32(m_Priority);
}

void
PacketPriorityTag::Deserialize(TagBuffer i){
  m_Priority = i.ReadU32();
}

uint32_t
PacketPriorityTag::GetSerializedSize() const{
  return sizeof(uint32_t);
}

void
PacketPriorityTag::Print(std::ostream &os) const
{
  os << "Priority=" << m_Priority;
}

TypeId
PacketPriorityTag::GetTypeId(){
  static TypeId tid = TypeId("PacketPriorityTag")
      .SetParent<Tag>()
      .AddConstructor<PacketPriorityTag>();
  return tid;
}

TypeId
PacketPriorityTag::GetInstanceTypeId() const{
  return GetTypeId();
}