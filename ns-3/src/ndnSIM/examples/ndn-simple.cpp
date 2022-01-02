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

// ndn-simple.cpp

#include "ns3/core-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

namespace ns3 {

    /**
     * This scenario simulates a very simple network topology:
     *
     *
     *      +----------+     1Mbps      +--------+     1Mbps      +----------+
     *      | consumer | <------------> | router | <------------> | producer |
     *      +----------+         10ms   +--------+          10ms  +----------+
     *
     *
     * Consumer requests data from producer with frequency 10 interests per second
     * (interests contain constantly increasing sequence number).
     *
     * For every received interest, producer replies with a data packet, containing
     * 1024 bytes of virtual payload.
     *
     * To run scenario and see what is happening, use the following command:
     *
     *     NS_LOG=ndn.Consumer:ndn.Producer ./waf --run=ndn-simple
     */

    int
    main(int argc, char* argv[])
    {
        // setting default parameters for PointToPoint links and channels
        Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("1Mbps"));
        /**
         * 把第一个参数按照最后一个::进行切割, 得到 "tidName = ns3::PointToPointNetDevice" 和 paramName = "DataRate"
         * ns3::IidManager 类的 m_namemap 里, 储存了 tidName 到 uid 的映射, 每个 uid 唯一表示了一个内容
         * 对于这个类,遍历它的attributes,如果匹配且输入值合法,即可执行SetAttributeInitialValue()
         * 其它类同
         */
        Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("10ms"));
        // 为了与SetDefault相对应,每个类都配备有相应的GetTypeId函数
        Config::SetDefault("ns3::QueueBase::MaxSize", StringValue("20p"));

        // Read optional command-line parameters (e.g., enable visualizer with ./waf --run=<> --visualize
        CommandLine cmd;
        cmd.Parse(argc, argv);
        // 例如: --abc=efg, 解析得到name="abc",value="efg"
        // 再有: --vis, 解析得到name="vis", value=""

        // Creating nodes
        // 创建N个节点, 这个容器类的成员就是一个装有节点指针的vector
        NodeContainer nodes;
        nodes.Create(3);

        // Connecting nodes using two links
        // 为节点创建p2p设备(网口),分配mac,创建队列等
        PointToPointHelper p2p;
        p2p.Install(nodes.Get(0), nodes.Get(1));
        p2p.Install(nodes.Get(1), nodes.Get(2));

        // Install NDN stack on all nodes
        ndn::StackHelper ndnHelper;
        ndnHelper.SetDefaultRoutes(true);
        ndnHelper.InstallAll();

        // Choosing forwarding strategy
        ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/multicast");

        // Installing applications

        // Consumer
        ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
        // Consumer will request /prefix/0, /prefix/1, ...
        consumerHelper.SetPrefix("/prefix");
        consumerHelper.SetAttribute("Frequency", StringValue("10")); // 10 interests a second

        // 上面的设置都存在m_factory里
        // 然后把这个m_factory安装到第0个节点上
        auto apps = consumerHelper.Install(nodes.Get(0)); // first node
        apps.Stop(Seconds(10.0));                         // stop the consumer app at 10 seconds mark

        // 总结: 用一个AppHelper设置好配置信息,将配置事件塞到事件队列里,然后等仿真开始时执行

        // Producer同理
        ndn::AppHelper producerHelper("ns3::ndn::Producer");
        // Producer will reply to all requests starting with /prefix
        producerHelper.SetPrefix("/prefix");
        producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
        producerHelper.Install(nodes.Get(2)); // last node

        // 设置了 20s 后的事件，这个事件令 m_stop = false
        Simulator::Stop(Seconds(20.0));

        // 执行 m_eventsWithContext 和 m_events 里的所有事件，直到事件空了，或者达到 m_stop
        // m_eventsWithContext 可以理解为 m_events 的缓冲队列，真正执行的是 m_event
        // 每次都从 m_eventsWithContext 抓出一个塞到 m_events 里）
        // 那么m_eventsWithContext是什么呢? 前面的各种ScheduleWithContext?
        Simulator::Run();

        // 执行 m_destroyEvents 里的所有事件
        Simulator::Destroy();

        return 0;
    }

} // namespace ns3

int
main(int argc, char* argv[])
{
    return ns3::main(argc, argv);
}
