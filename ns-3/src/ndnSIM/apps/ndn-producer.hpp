/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef NDN_PRODUCER_H
#define NDN_PRODUCER_H

#include "ns3/ndnSIM/model/ndn-common.hpp"

#include "ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-common.hpp"

#include "ns3/nstime.h"
#include "ns3/ptr.h"

namespace ns3 {
namespace ndn {

/**
 * @ingroup ndn-apps
 * @brief A simple Interest-sink applia simple Interest-sink application
 *
 * A simple Interest-sink applia simple Interest-sink application,
 * which replying every incoming Interest with Data packet with a specified
 * size and name same as in Interest.cation, which replying every incoming Interest
 * with Data packet with a specified size and name same as in Interest.
 */
class Producer : public App {
  public:
    // 接口，返回关于本类对象的TypeId信息，方便外部配置
    static TypeId GetTypeId(void);

    Producer();

    // inherited from NdnApp
    // 收到interest包后，装载一个data包（内部有编码为block的成员），最后把任务交接给到m_appLink->onReceiveData
    // NOTE: 这是应用层收包
    virtual void OnInterest(shared_ptr<const Interest> interest);

  protected:
    // inherited from Application base class.
    // 调用App创建相关的App管理接口，如AppLinkService这种东西，并把管理链路层和传输层的接口放到Ndn Stack里
    virtual void StartApplication(); // Called at time specified by Start

    // m_active = false并关闭m_face
    virtual void StopApplication(); // Called at time specified by Stop

  private:
    Name m_prefix;                 // 前缀
    Name m_postfix;                // 后缀???
    uint32_t m_virtualPayloadSize; // 一个包装载数据byte数
    Time m_freshness;              // 新鲜时间???

    uint32_t m_signature;
    Name m_keyLocator;
};

} // namespace ndn
} // namespace ns3

#endif // NDN_PRODUCER_H
