/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2019,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "link-service.hpp"
#include "face.hpp"

namespace nfd {
namespace face {

NFD_LOG_INIT(LinkService);

LinkService::LinkService()
  : m_face(nullptr)
  , m_transport(nullptr)
{
}

LinkService::~LinkService()
{
}

void
LinkService::setFaceAndTransport(Face& face, Transport& transport)
{
  BOOST_ASSERT(m_face == nullptr);
  BOOST_ASSERT(m_transport == nullptr);

  m_face = &face;
  m_transport = &transport;
}

void
LinkService::sendInterest(const Interest& interest, const EndpointId& endpoint)
{
  BOOST_ASSERT(m_transport != nullptr);
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nOutInterests;

  // 对于ndn-simple.cpp中的节点2来说,作为producer,它具有App特性
  // 因此其链路层是AppLinkService,而非GenericLinkService
  // TODO: 这个是在哪里设置的?
  doSendInterest(interest, endpoint);

  afterSendInterest(interest);
}

void
LinkService::sendData(const Data& data, const EndpointId& endpoint)
{
  BOOST_ASSERT(m_transport != nullptr);
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nOutData;

  doSendData(data, endpoint);

  afterSendData(data);
}

void
LinkService::sendNack(const ndn::lp::Nack& nack, const EndpointId& endpoint)
{
  BOOST_ASSERT(m_transport != nullptr);
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nOutNacks;

  doSendNack(nack, endpoint);

  afterSendNack(nack);
}

void
LinkService::receiveInterest(const Interest& interest, const EndpointId& endpoint)
{
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nInInterests;

  // 然后到这里就和之前的章节连上了 ↓↓↓

  // 触发信号 afterReceiveInterest
  // 该信号连接到 Forwarder::startProcessInterest !  
  // TODO: 怎么连接的? 通过Forwarder的构造函数
  // 里面的 faceTable 将里面每个 face 的 afterReceiveInterest 信号都连接到了 Forwarder::startProcessInterest 上
  afterReceiveInterest(interest, endpoint);
}

void
LinkService::receiveData(const Data& data, const EndpointId& endpoint)
{
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nInData;

  // 触发信号afterReceiveData,该信号连接到Forwarder::startProcessData 
  afterReceiveData(data, endpoint);
}

void
LinkService::receiveNack(const ndn::lp::Nack& nack, const EndpointId& endpoint)
{
  NFD_LOG_FACE_TRACE(__func__);

  ++this->nInNacks;

  afterReceiveNack(nack, endpoint);
}

void
LinkService::notifyDroppedInterest(const Interest& interest)
{
  ++this->nDroppedInterests;
  onDroppedInterest(interest);
}

std::ostream&
operator<<(std::ostream& os, const FaceLogHelper<LinkService>& flh)
{
  const Face* face = flh.obj.getFace();
  if (face == nullptr) {
    os << "[id=0,local=unknown,remote=unknown] ";
  }
  else {
    os << "[id=" << face->getId() << ",local=" << face->getLocalUri()
       << ",remote=" << face->getRemoteUri() << "] ";
  }
  return os;
}

} // namespace face
} // namespace nfd
