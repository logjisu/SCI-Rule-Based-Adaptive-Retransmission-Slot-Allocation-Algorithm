/* a-packet-tags.h */

#ifndef PACKET_TAGS_H
#define PACKET_TAGS_H

#include "ns3/tag.h"
#include "ns3/nstime.h"
#include "ns3/uinteger.h"
#include <iostream>

using namespace ns3;

// 패킷 생성 시간 태그 클래스
class PacketCreationTimeTag : public Tag
{
public:
  PacketCreationTimeTag();
  PacketCreationTimeTag(uint64_t time);

  void SetCreationTime(uint64_t time);
  uint64_t GetCreationTime() const;

  void Serialize(TagBuffer i) const override;
  void Deserialize(TagBuffer i) override;
  uint32_t GetSerializedSize() const override;
  void Print(std::ostream &os) const override;

  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;

private:
  uint64_t m_creationTime;
};

// UE ID 태그 클래스
class PacketUeIdTag : public Tag
{
public:
  PacketUeIdTag();
  PacketUeIdTag(uint32_t ueId);

  void SetUeId(uint32_t ueId);
  uint32_t GetUeId() const;

  void Serialize(TagBuffer i) const override;
  void Deserialize(TagBuffer i) override;
  uint32_t GetSerializedSize() const override;
  void Print(std::ostream &os) const override;

  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;

private:
  uint32_t m_ueId;
};

// 긴급 패킷 태그 클래스
class PacketUrgencyTag : public Tag
{
public:
  PacketUrgencyTag();
  PacketUrgencyTag(uint32_t Urgency);

  void SetUrgency(uint32_t Urgency);
  uint32_t GetUrgency() const;

  void Serialize(TagBuffer i) const override;
  void Deserialize(TagBuffer i) override;
  uint32_t GetSerializedSize() const override;
  void Print(std::ostream &os) const override;

  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;

private:
  uint32_t m_Urgency;
};

#endif // PACKET_TAGS_H