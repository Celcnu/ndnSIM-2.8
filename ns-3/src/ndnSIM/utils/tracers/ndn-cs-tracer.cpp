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

#include "ndn-cs-tracer.hpp"
#include "ns3/callback.h"
#include "ns3/config.h"
#include "ns3/names.h"
#include "ns3/node.h"
#include "ns3/packet.h"

#include "apps/ndn-app.hpp"
#include "ns3/log.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"

#include <boost/lexical_cast.hpp>

#include <fstream>

// chaochao
#include "fw/forwarder.hpp"
#include "model/ndn-l3-protocol.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.CsTracer");

namespace ns3 {
namespace ndn {

static std::list<std::tuple<shared_ptr<std::ostream>, std::list<Ptr<CsTracer>>>> g_tracers;

void
CsTracer::Destroy()
{
    g_tracers.clear();
}

void
CsTracer::InstallAll(const std::string& file, Time averagingPeriod /* = Seconds (0.5)*/)
{
    // ndn::CsTracer::InstallAll("cs-trace.txt", Seconds(1));
    using namespace boost;
    using namespace std;

    std::list<Ptr<CsTracer>> tracers;
    // 打开文件准备写入
    shared_ptr<std::ostream> outputStream;
    if (file != "-") {
        shared_ptr<std::ofstream> os(new std::ofstream());
        os->open(file.c_str(), std::ios_base::out | std::ios_base::trunc);

        if (!os->is_open()) {
            NS_LOG_ERROR("File " << file << " cannot be opened for writing. Tracing disabled");
            return;
        }

        outputStream = os;
    }
    else {
        outputStream = shared_ptr<std::ostream>(&std::cout, std::bind([] {}));
    }

    // 遍历每个节点 每个节点安装CsTracer
    for (NodeList::Iterator node = NodeList::Begin(); node != NodeList::End(); node++) {
        Ptr<CsTracer> trace = Install(*node, outputStream, averagingPeriod);
        tracers.push_back(trace);
    }

    if (tracers.size() > 0) {
        // *m_l3RateTrace << "# "; // not necessary for R's read.table
        tracers.front()->PrintHeader(*outputStream);
        *outputStream << "\n";
    }

    g_tracers.push_back(std::make_tuple(outputStream, tracers));
}

void
CsTracer::Install(const NodeContainer& nodes, const std::string& file, Time averagingPeriod /* = Seconds (0.5)*/)
{
    using namespace boost;
    using namespace std;

    std::list<Ptr<CsTracer>> tracers;
    shared_ptr<std::ostream> outputStream;
    if (file != "-") {
        shared_ptr<std::ofstream> os(new std::ofstream());
        os->open(file.c_str(), std::ios_base::out | std::ios_base::trunc);

        if (!os->is_open()) {
            NS_LOG_ERROR("File " << file << " cannot be opened for writing. Tracing disabled");
            return;
        }

        outputStream = os;
    }
    else {
        outputStream = shared_ptr<std::ostream>(&std::cout, std::bind([] {}));
    }

    for (NodeContainer::Iterator node = nodes.Begin(); node != nodes.End(); node++) {
        Ptr<CsTracer> trace = Install(*node, outputStream, averagingPeriod);
        tracers.push_back(trace);
    }

    if (tracers.size() > 0) {
        // *m_l3RateTrace << "# "; // not necessary for R's read.table
        tracers.front()->PrintHeader(*outputStream);
        *outputStream << "\n";
    }

    g_tracers.push_back(std::make_tuple(outputStream, tracers));
}

void
CsTracer::Install(Ptr<Node> node, const std::string& file, Time averagingPeriod /* = Seconds (0.5)*/)
{
    using namespace boost;
    using namespace std;

    std::list<Ptr<CsTracer>> tracers;
    shared_ptr<std::ostream> outputStream;
    if (file != "-") {
        shared_ptr<std::ofstream> os(new std::ofstream());
        os->open(file.c_str(), std::ios_base::out | std::ios_base::trunc);

        if (!os->is_open()) {
            NS_LOG_ERROR("File " << file << " cannot be opened for writing. Tracing disabled");
            return;
        }

        outputStream = os;
    }
    else {
        outputStream = shared_ptr<std::ostream>(&std::cout, std::bind([] {}));
    }

    Ptr<CsTracer> trace = Install(node, outputStream, averagingPeriod);
    tracers.push_back(trace);

    if (tracers.size() > 0) {
        // *m_l3RateTrace << "# "; // not necessary for R's read.table
        tracers.front()->PrintHeader(*outputStream);
        *outputStream << "\n";
    }

    g_tracers.push_back(std::make_tuple(outputStream, tracers));
}

Ptr<CsTracer>
CsTracer::Install(Ptr<Node> node, shared_ptr<std::ostream> outputStream, Time averagingPeriod /* = Seconds (0.5)*/)
{
    NS_LOG_DEBUG("Node: " << node->GetId());

    Ptr<CsTracer> trace = Create<CsTracer>(outputStream, node);
    trace->SetAveragingPeriod(averagingPeriod);

    return trace;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

CsTracer::CsTracer(shared_ptr<std::ostream> os, Ptr<Node> node)
  : m_nodePtr(node)
  , m_os(os)
{
    m_node = boost::lexical_cast<std::string>(m_nodePtr->GetId());

    Connect();

    std::string name = Names::FindName(node);
    if (!name.empty()) {
        m_node = name;
    }
}

CsTracer::CsTracer(shared_ptr<std::ostream> os, const std::string& node)
  : m_node(node)
  , m_os(os)
{
    Connect();
}

CsTracer::~CsTracer(){};

// 在构造函数中调用 --> Reset --> 计数值清 0
void
CsTracer::Connect()
{
    // @TODO Do the same with NFD content store...
    // Ptr<ContentStore> cs = m_nodePtr->GetObject<ContentStore>();
    // cs->TraceConnectWithoutContext("CacheHits", MakeCallback(&CsTracer::CacheHits, this));
    // cs->TraceConnectWithoutContext("CacheMisses", MakeCallback(&CsTracer::CacheMisses, this));

    // TODO: 需要将hit/miss和转发的pipeline回调绑定! 类似上面的实现 ↑
    // NFD 中的CS是怎么实现的?

    auto l3proto = m_nodePtr->GetObject<ndn::L3Protocol>();
    auto fwd = l3proto->getForwarder();
    // // 你都拿到fowarder了, 不能直接去读它的counter吗 ???

    fwd->afterCsHit.connect([this](Interest interest, Data data) { CacheHits(interest, data); });
    fwd->afterCsMiss.connect([this](Interest interest) { CacheMisses(interest); });

    Reset();
}

// 设置平均时间 (即统计周期,多长时间写入一次)
void
CsTracer::SetAveragingPeriod(const Time& period)
{
    m_period = period;
    m_printEvent.Cancel();
    //  m_period时间之后, 将事件PeriodicPrinter, 添加到事件队列
    m_printEvent = Simulator::Schedule(m_period, &CsTracer::PeriodicPrinter, this);
}

void
CsTracer::PeriodicPrinter()
{
    // NS_LOG_INFO("chaochao printer()");

    // // chaochao (deleted)
    // auto fwd = (m_nodePtr->GetObject<ndn::L3Protocol>())->getForwarder();
    // const auto& fwdCounters = fwd ->getCounters();
    // m_stats.m_cacheHits = fwdCounters.nCsHits;
    // m_stats.m_cacheMisses = fwdCounters.nCsMisses;

    Print(*m_os);
    Reset(); // 每一个周期单独统计

    // 添加下一个打印事件(即下一个周期再次回来打印, 一直循环)
    m_printEvent = Simulator::Schedule(m_period, &CsTracer::PeriodicPrinter, this);
}

void
CsTracer::PrintHeader(std::ostream& os) const
{
    os << "Time"
       << "\t"

       << "Node"
       << "\t"

       << "Type"
       << "\t"
       << "Packets"
       << "\t";
}

void
CsTracer::Reset()
{
    m_stats.Reset();
}

#define PRINTER(printName, fieldName)                                                                                  \
    os << time.ToDouble(Time::S) << "\t" << m_node << "\t" << printName << "\t" << m_stats.fieldName << "\n";

void
CsTracer::Print(std::ostream& os) const
{
    Time time = Simulator::Now();

    PRINTER("CacheHits", m_cacheHits);
    PRINTER("CacheMisses", m_cacheMisses);
}

// 什么时候会调用到这个? 在哪里调用?
// TODO: 缓存命中时, 如何触发对应的tracer?
void
CsTracer::CacheHits(const Interest&, const Data&)
{
    m_stats.m_cacheHits++;
    NS_LOG_INFO("chaochao hits++");
}

void
CsTracer::CacheMisses(const Interest&)
{
    m_stats.m_cacheMisses++;
    NS_LOG_INFO("chaochao misses++");
}

} // namespace ndn
} // namespace ns3
