/* a-packet-tags.cc */

#include "a-packet-tags.h"

// PacketCreationTimeTag 구현
PacketCreationTimeTag::PacketCreationTimeTag() : m_creationTime(0) {}
PacketCreationTimeTag::PacketCreationTimeTag(uint64_t time) : m_creationTime(time) {}

void PacketCreationTimeTag::SetCreationTime(uint64_t time)
{
  m_creationTime = time;
}

uint64_t PacketCreationTimeTag::GetCreationTime() const
{
  return m_creationTime;
}

void PacketCreationTimeTag::Serialize(TagBuffer i) const
{
  i.WriteU64(m_creationTime);
}

void PacketCreationTimeTag::Deserialize(TagBuffer i)
{
  m_creationTime = i.ReadU64();
}

uint32_t PacketCreationTimeTag::GetSerializedSize() const
{
  return sizeof(uint64_t);
}

void PacketCreationTimeTag::Print(std::ostream &os) const
{
  os << "CreationTime=" << m_creationTime;
}

TypeId PacketCreationTimeTag::GetTypeId()
{
  static TypeId tid = TypeId("PacketCreationTimeTag")
      .SetParent<Tag>()
      .AddConstructor<PacketCreationTimeTag>();
  return tid;
}

TypeId PacketCreationTimeTag::GetInstanceTypeId() const
{
  return GetTypeId();
}

// PacketUeIdTag 구현
PacketUeIdTag::PacketUeIdTag() : m_ueId(0) {}
PacketUeIdTag::PacketUeIdTag(uint32_t ueId) : m_ueId(ueId) {}

void PacketUeIdTag::SetUeId(uint32_t ueId)
{
  m_ueId = ueId;
}

uint32_t PacketUeIdTag::GetUeId() const
{
  return m_ueId;
}

void PacketUeIdTag::Serialize(TagBuffer i) const
{
  i.WriteU32(m_ueId);
}

void PacketUeIdTag::Deserialize(TagBuffer i)
{
  m_ueId = i.ReadU32();
}

uint32_t PacketUeIdTag::GetSerializedSize() const
{
  return sizeof(uint32_t);
}

void PacketUeIdTag::Print(std::ostream &os) const
{
  os << "UeId=" << m_ueId;
}

TypeId PacketUeIdTag::GetTypeId()
{
  static TypeId tid = TypeId("PacketUeIdTag")
      .SetParent<Tag>()
      .AddConstructor<PacketUeIdTag>();
  return tid;
}

TypeId PacketUeIdTag::GetInstanceTypeId() const
{
  return GetTypeId();
}

// PacketUrgencyTag 구현
PacketUrgencyTag::PacketUrgencyTag() : m_Urgency(0) {}
PacketUrgencyTag::PacketUrgencyTag(uint32_t Urgency) : m_Urgency(Urgency) {}

void PacketUrgencyTag::SetUrgency(uint32_t Urgency)
{
  m_Urgency = Urgency;
}

uint32_t PacketUrgencyTag::GetUrgency() const
{
  return m_Urgency;
}

void PacketUrgencyTag::Serialize(TagBuffer i) const
{
  i.WriteU32(m_Urgency);
}

void PacketUrgencyTag::Deserialize(TagBuffer i)
{
  m_Urgency = i.ReadU32();
}

uint32_t PacketUrgencyTag::GetSerializedSize() const
{
  return sizeof(uint32_t);
}

void PacketUrgencyTag::Print(std::ostream &os) const
{
  os << "Urgency=" << m_Urgency;
}

TypeId PacketUrgencyTag::GetTypeId()
{
  static TypeId tid = TypeId("PacketUrgencyTag")
      .SetParent<Tag>()
      .AddConstructor<PacketUrgencyTag>();
  return tid;
}

TypeId PacketUrgencyTag::GetInstanceTypeId() const
{
  return GetTypeId();
}