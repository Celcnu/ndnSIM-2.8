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

#include "forwarder.hpp"

#include "algorithm.hpp"
#include "best-route-strategy2.hpp"
#include "common/global.hpp"
#include "common/logger.hpp"
#include "strategy.hpp"
#include "table/cleanup.hpp"

#include <ndn-cxx/lp/tags.hpp>

#include "face/null-face.hpp"

namespace nfd {

NFD_LOG_INIT(Forwarder);

static Name
getDefaultStrategyName()
{
    return fw::BestRouteStrategy2::getStrategyName();
}

// TODO: 这些成员变量各自代表的含义?
// 构造函数 ---> 主要是在连接各种信号
Forwarder::Forwarder(FaceTable& faceTable)
  : m_faceTable(faceTable)
  , m_unsolicitedDataPolicy(make_unique<fw::DefaultUnsolicitedDataPolicy>())
  , m_fib(m_nameTree)
  , m_pit(m_nameTree)
  , m_measurements(m_nameTree)
  , m_strategyChoice(*this)
  , m_csFace(face::makeNullFace(FaceUri("contentstore://")))
{
    // 给 m_faceTable 添加保留一个接口( face ) contentstore:// (旧版本代码没有这一行)
    m_faceTable.addReserved(m_csFace, face::FACEID_CONTENT_STORE);

    // 让 m_faceTable 的 afterAdd 信号装配操作(即使得每次新添加的 face 的各信号能连接到对应的
    // Forwarder 的函数/管道)
    m_faceTable.afterAdd.connect([this](const Face& face) {
        // 让每个 face.afterReceiveInterest 连接到 this->startProcessInterest （处理兴趣包）
        face.afterReceiveInterest.connect(
          [this, &face](const Interest& interest, const EndpointId& endpointId) {
              this->startProcessInterest(FaceEndpoint(face, endpointId), interest);
          });

        // 让每个 face.afterReceiveData 连接到 this->startProcessData （处理数据包）
        face.afterReceiveData.connect(
          [this, &face](const Data& data, const EndpointId& endpointId) {
              this->startProcessData(FaceEndpoint(face, endpointId), data);
          });

        // 让每个 face.afterReceiveNack 连接到 this->startProcessNack （处理Nack）
        face.afterReceiveNack.connect(
          [this, &face](const lp::Nack& nack, const EndpointId& endpointId) {
              this->startProcessNack(FaceEndpoint(face, endpointId), nack);
          });

        // 让每个 face.onDroppedInterest 连接到 this->onDroppedInterest （往FIB添加新路由时）
        face.onDroppedInterest.connect([this, &face](const Interest& interest) {
            this->onDroppedInterest(FaceEndpoint(face, 0), interest);
        });
    });

    // 给 m_faceTable 的 beforeRemove 信号装配操作： cleanupOnFaceRemoval （每次移除 face 前先
    // cleanup ）
    m_faceTable.beforeRemove.connect(
      [this](const Face& face) { cleanupOnFaceRemoval(m_nameTree, m_fib, m_pit, face); });

    // 给 m_fib 的 afterNewNextHop 信号装配操作： this->startProcessNewNextHop
    m_fib.afterNewNextHop.connect([&](const Name& prefix, const fib::NextHop& nextHop) {
        this->startProcessNewNextHop(prefix, nextHop);
    });

    // 设置 m_strategyChoice 为默认的 fw::BestRouteStrategy2
    m_strategyChoice.setDefaultStrategy(getDefaultStrategyName());
}

Forwarder::~Forwarder() = default;

void
Forwarder::onIncomingInterest(const FaceEndpoint& ingress, const Interest& interest)
{
    // receive Interest
    NFD_LOG_DEBUG("onIncomingInterest in=" << ingress << " interest=" << interest.getName());
    // 给interest包打上IncomingFaceId标签
    interest.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
    ++m_counters.nInInterests;

    // /localhost scope control, "localhost"前缀只用于本地通信
    // 如果face不是local, 同时interest的Name的前缀又是"localhost", 则drop
    bool isViolatingLocalhost = ingress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL
                                && scope_prefix::LOCALHOST.isPrefixOf(interest.getName());
    if (isViolatingLocalhost) {
        NFD_LOG_DEBUG("onIncomingInterest in=" << ingress << " interest=" << interest.getName()
                                               << " violates /localhost");
        // (drop)
        return;
    }

    // detect duplicate Nonce with Dead Nonce List, 这个死亡随机数列表是干嘛的?
    // 检查m_deadNonceList里有没有与当前包一样的内容, 如果有, 说明interest循环了(环路),
    // 执行this->onInterestLoop
    bool hasDuplicateNonceInDnl = m_deadNonceList.has(interest.getName(), interest.getNonce());
    if (hasDuplicateNonceInDnl) {
        // goto Interest loop pipeline
        this->onInterestLoop(ingress, interest);
        return;
    }

    // strip forwarding hint if Interest has reached producer region
    // 如果interest传到了Producer的地方, 就剥掉forwardingHint, TODO: 这个hint是用来干嘛的?
    // 到达源端节点? 什么叫region啊?
    if (!interest.getForwardingHint().empty()
        && m_networkRegionTable.isInProducerRegion(interest.getForwardingHint())) {
        NFD_LOG_DEBUG("onIncomingInterest in=" << ingress << " interest=" << interest.getName()
                                               << " reaching-producer-region");
        const_cast<Interest&>(interest).setForwardingHint({});
    }

    // PIT insert
    // 尝试将interest插入PIT表(如果在PIT找到了该interest，自然就不必插入了? 不需要看不同的接口吗?
    // interest里面标识了不同的入口)
    shared_ptr<pit::Entry> pitEntry = m_pit.insert(interest).first;

    // detect duplicate Nonce in PIT entry
    // 检测PIT里有没有相同的Nonce, TODO: 这个Nonce随机数是用来干嘛的?
    int dnw = fw::findDuplicateNonce(*pitEntry, interest.getNonce(), ingress.face);
    bool hasDuplicateNonceInPit = dnw != fw::DUPLICATE_NONCE_NONE;
    if (ingress.face.getLinkType() == ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
        // for p2p face: duplicate Nonce from same incoming face is not loop
        // p2p face的duplicate Nonce不被看作Loop
        hasDuplicateNonceInPit = hasDuplicateNonceInPit && !(dnw & fw::DUPLICATE_NONCE_IN_SAME);
    }

    // 如果重复了，说明interest循环了(环路), 执行this->onInterestLoop
    if (hasDuplicateNonceInPit) {
        // goto Interest loop pipeline
        this->onInterestLoop(ingress, interest);
        this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
            strategy.afterReceiveLoopedInterest(ingress, interest, *pitEntry);
        });
        return;
    }

    // is pending?
    // 请注意这里采用的是先查PIT, 再查CS的方式, 和原生NDN不同
    // 原因在于:
    // 如果PIT匹配到了(所以请求才会在这里pending),说明本地CS肯定没有,可以节省开销,直接不用查了
    // 如果PIT中没匹配到, 说明这是一个新来的请求, 再去查CS中有没有
    if (!pitEntry->hasInRecords()) {
        m_cs.find(interest, bind(&Forwarder::onContentStoreHit, this, ingress, pitEntry, _1, _2),
                  bind(&Forwarder::onContentStoreMiss, this, ingress, pitEntry, _1));
    }
    else {
        this->onContentStoreMiss(ingress, pitEntry, interest);
    }
}

void
Forwarder::onInterestLoop(const FaceEndpoint& ingress, const Interest& interest)
{
    // if multi-access or ad hoc face, drop
    if (ingress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
        NFD_LOG_DEBUG("onInterestLoop in=" << ingress << " interest=" << interest.getName()
                                           << " drop");
        return;
    }

    NFD_LOG_DEBUG("onInterestLoop in=" << ingress << " interest=" << interest.getName()
                                       << " send-Nack-duplicate");

    // send Nack with reason=DUPLICATE
    // note: Don't enter outgoing Nack pipeline because it needs an in-record.
    // p2p? p2p链路的兴趣包循环相当于NACK
    lp::Nack nack(interest);
    nack.setReason(lp::NackReason::DUPLICATE);
    ingress.face.sendNack(nack, ingress.endpoint);
}

void
Forwarder::onContentStoreMiss(const FaceEndpoint& ingress, const shared_ptr<pit::Entry>& pitEntry,
                              const Interest& interest)
{
    NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName());
    ++m_counters.nCsMisses;
    afterCsMiss(interest);

    // insert in-record
    // 给pitEntry加上InRecord (表示兴趣包在这个接口传进来,并且正在pending)
    pitEntry->insertOrUpdateInRecord(ingress.face, interest);

    // set PIT expiry timer to the time that the last PIT in-record expires
    // 更新到期时间为最后一个PIT in-record的到期时间, 什么叫最后一个???
    auto lastExpiring =
      std::max_element(pitEntry->in_begin(), pitEntry->in_end(),
                       [](const auto& a, const auto& b) { return a.getExpiry() < b.getExpiry(); });
    auto lastExpiryFromNow = lastExpiring->getExpiry() - time::steady_clock::now();
    this->setExpiryTimer(pitEntry, time::duration_cast<time::milliseconds>(lastExpiryFromNow));

    // has NextHopFaceId?
    // 如果在兴趣包中显式指定了下一跳路由 TODO: 为什么会在兴趣包中指定? 源路由?
    auto nextHopTag = interest.getTag<lp::NextHopFaceIdTag>();
    if (nextHopTag != nullptr) {
        // chosen NextHop face exists?
        Face* nextHopFace = m_faceTable.get(*nextHopTag);
        if (nextHopFace != nullptr) {
            NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName() << " nexthop-faceid="
                                                         << nextHopFace->getId());
            // go to outgoing Interest pipeline
            // scope control is unnecessary, because privileged app explicitly wants to forward
            this->onOutgoingInterest(pitEntry, FaceEndpoint(*nextHopFace, 0), interest);
        }
        return;
    }

    // dispatch to strategy: after incoming Interest
    // 否则: 根据pitEntry的状态信息，选择收到interest后的转发/不转发策略
    this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
        strategy.afterReceiveInterest(FaceEndpoint(ingress.face, 0), interest, pitEntry);
    });
}

// CS缓存命中将会触发
// 不过以 ndn-simple.cpp 为例,因为包里面一个Payload的 1024B 里是随机的垃圾数据,所以基本上不可能出现
// CS 命中
// TODO: 命中是匹配名字, 和payload有什么关系呢?
void
Forwarder::onContentStoreHit(const FaceEndpoint& ingress, const shared_ptr<pit::Entry>& pitEntry,
                             const Interest& interest, const Data& data)
{
    NFD_LOG_DEBUG("onContentStoreHit interest=" << interest.getName());
    ++m_counters.nCsHits;
    afterCsHit(interest, data);

    // 给数据包打上tag. 记录它进来的那个端口
    data.setTag(make_shared<lp::IncomingFaceIdTag>(face::FACEID_CONTENT_STORE));
    // FIXME Should we lookup PIT for other Interests that also match the data?

    pitEntry->isSatisfied = true; // 这是哪个pitEntry??? 命中节点上的吗?
    pitEntry->dataFreshnessPeriod =
      data.getFreshnessPeriod(); // 更新dataFreshnessPeriod --> 因为它又被重新使用了,变得更新鲜了?

    // set PIT expiry timer to now
    // 将这个pitEntry的到期时间设置为现在,到时候执行onInterestFinalize
    // 到时候???
    this->setExpiryTimer(pitEntry, 0_ms);

    beforeSatisfyInterest(*pitEntry, *m_csFace, data);
    this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
        strategy.beforeSatisfyInterest(pitEntry, FaceEndpoint(*m_csFace, 0), data);
    });

    // dispatch to strategy: after Content Store hit
    // 执行Strategy::sendData->Forwarder::onOutgoingData
    // 怎么理解这里的strategy? 不同的策略对应一组不同的操作?
    this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
        strategy.afterContentStoreHit(pitEntry, ingress, data);
    });
}

// 如果决定转发interest, 就会调用本函数, 即它是完成执行转发动作的
void
Forwarder::onOutgoingInterest(const shared_ptr<pit::Entry>& pitEntry, const FaceEndpoint& egress,
                              const Interest& interest)
{
    NFD_LOG_DEBUG("onOutgoingInterest out=" << egress << " interest=" << pitEntry->getName());

    // insert out-record
    // 首先插入一个out-record（如果已经存在相同的接口就更新），记录下最后一个包的随机数和到期时间
    // 给pitEntry加上out-record???
    // TODO: out-record???
    pitEntry->insertOrUpdateOutRecord(egress.face, interest);

    // send Interest
    // 交给链路层执行doSendInterest TODO: 怎么在这个过程中间插入自定义的字段?
    egress.face.sendInterest(interest, egress.endpoint);
    ++m_counters.nOutInterests;
}

// 这个兴趣已经被搞定之后的操作, 什么时候会调用它???
void
Forwarder::onInterestFinalize(const shared_ptr<pit::Entry>& pitEntry)
{
    NFD_LOG_DEBUG("onInterestFinalize interest="
                  << pitEntry->getName()
                  << (pitEntry->isSatisfied ? " satisfied" : " unsatisfied"));

    if (!pitEntry->isSatisfied) {
        beforeExpirePendingInterest(*pitEntry);
    }

    // Dead Nonce List insert if necessary
    // 如果MustBeFresh, 且FreshnessPeriod小于LifeTime, 则需要插入DeadNonceList
    // 这个兴趣包指定 必须要在指定时间内得到响应 否则就没有用了
    this->insertDeadNonceList(*pitEntry, nullptr);

    // Increment satisfied/unsatisfied Interests counter
    if (pitEntry->isSatisfied) {
        ++m_counters.nSatisfiedInterests;
    }
    else {
        ++m_counters.nUnsatisfiedInterests;
    }

    // PIT delete
    // 删除PIT里对应的条目
    pitEntry->expiryTimer.cancel();
    m_pit.erase(pitEntry.get());
}

// 接收到 Data 包之后的处理, 即给PIT的每个in-record都发包
void
Forwarder::onIncomingData(const FaceEndpoint& ingress, const Data& data)
{
    // receive Data
    // 给data包打上IncomingFaceId标签, 指示它从哪个接口传回来的
    // 这个tag有什么用???
    NFD_LOG_DEBUG("onIncomingData in=" << ingress << " data=" << data.getName());
    data.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
    ++m_counters.nInData;

    // /localhost scope control
    // 如果face不是local，同时interest的Name的前缀又是"localhost"，则drop
    bool isViolatingLocalhost = ingress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL
                                && scope_prefix::LOCALHOST.isPrefixOf(data.getName());
    if (isViolatingLocalhost) {
        NFD_LOG_DEBUG("onIncomingData in=" << ingress << " data=" << data.getName()
                                           << " violates /localhost");
        // (drop)
        return;
    }

    // PIT match
    // 如果PIT表里没有对应条目，说明data是不请自来的，执行onDataUnsolicited
    pit::DataMatchResult pitMatches = m_pit.findAllDataMatches(data);
    if (pitMatches.size() == 0) {
        // goto Data unsolicited pipeline
        this->onDataUnsolicited(ingress, data);
        return;
    }

    // CS insert
    // 尝试往CS里插入data
    // TODO: 缓存决策, 你可以直接在这里实现, 也可以在insert里面实现
    m_cs.insert(data);

    // when only one PIT entry is matched, trigger strategy: after receive Data
    // 只匹配到1个PIT条目
    if (pitMatches.size() == 1) {
        auto& pitEntry = pitMatches.front();

        NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

        // set PIT expiry timer to now
        // 设置PIT到期(即这个PIT已经搞定了,不需要继续pending了), 准备onOutgoingData
        this->setExpiryTimer(pitEntry, 0_ms);

        beforeSatisfyInterest(*pitEntry, ingress.face, data);
        // trigger strategy: after receive Data
        // 调用afterReceiveData，这将执行Strategy::sendDataToAll (最终调用到onOutgoingData)
        // pitEntry里面记录了不同兴趣包传入的接口
        this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
            strategy.afterReceiveData(pitEntry, ingress, data);
        });

        // mark PIT satisfied
        // 设置pitEntry为已满足 (从而有可能添加到DeadNonceList)
        pitEntry->isSatisfied = true;
        pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

        // 死亡随机数列表???
        // Dead Nonce List insert if necessary (for out-record of inFace)
        // 如果MustBeFresh，且FreshnessPeriod小于LifeTime，则需要插入DeadNonceList
        this->insertDeadNonceList(*pitEntry, &ingress.face);

        // delete PIT entry's out-record
        // 删掉该PIT条目的out-record
        pitEntry->deleteOutRecord(ingress.face);
    }
    // when more than one PIT entry is matched, trigger strategy: before satisfy Interest,
    // and send Data to all matched out faces
    // 匹配到多个PIT条目 ---> TODO: 这是由不同的转发策略造成的, 不是我理解的1个pitEntry的多个入口
    else {
        std::set<std::pair<Face*, EndpointId>> pendingDownstreams;
        auto now = time::steady_clock::now();

        // 遍历把每个的in-record添加到“下一跳们”（后续集中处理）,不是下一跳们,是"有请求传入的接口们"
        for (const auto& pitEntry : pitMatches) {
            NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

            // remember pending downstreams
            for (const pit::InRecord& inRecord : pitEntry->getInRecords()) {
                if (inRecord.getExpiry() > now) {
                    pendingDownstreams.emplace(&inRecord.getFace(), 0);
                }
            }

            // set PIT expiry timer to now
            this->setExpiryTimer(pitEntry, 0_ms);

            // invoke PIT satisfy callback
            beforeSatisfyInterest(*pitEntry, ingress.face, data);
            this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
                strategy.beforeSatisfyInterest(pitEntry, ingress, data);
            });

            // mark PIT satisfied
            pitEntry->isSatisfied = true;
            pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

            // Dead Nonce List insert if necessary (for out-record of inFace)
            this->insertDeadNonceList(*pitEntry, &ingress.face);

            // clear PIT entry's in and out records
            // 清除掉in-record
            // TODO: PIT的in-records? out-record? 分别代表什么?
            pitEntry->clearInRecords();
            pitEntry->deleteOutRecord(ingress.face);
        }

        // foreach pending downstream
        // 对于“下一跳们”的每一个，都执行onOutgoingData
        for (const auto& pendingDownstream : pendingDownstreams) {
            if (pendingDownstream.first->getId() == ingress.face.getId()
                && pendingDownstream.second == ingress.endpoint
                && pendingDownstream.first->getLinkType() != ndn::nfd::LINK_TYPE_AD_HOC) {
                continue;
            }
            // goto outgoing Data pipeline
            this->onOutgoingData(data,
                                 FaceEndpoint(*pendingDownstream.first, pendingDownstream.second));
        }
    }
}

// 如果数据是不请自来的，执行本函数。看看对于不请自来的数据的策略是啥，如果策略是缓存，那么就尝试插入CS。否则啥都不干。
void
Forwarder::onDataUnsolicited(const FaceEndpoint& ingress, const Data& data)
{
    // accept to cache?
    // 如果数据是不请自来的，则根据 m_unsolicitedDataPolicy
    // 的决定来判断。如果认为应当缓存就尝试缓存，否则啥都不干
    fw::UnsolicitedDataDecision decision = m_unsolicitedDataPolicy->decide(ingress.face, data);
    if (decision == fw::UnsolicitedDataDecision::CACHE) {
        // CS insert
        m_cs.insert(data, true);
    }

    NFD_LOG_DEBUG("onDataUnsolicited in=" << ingress << " data=" << data.getName()
                                          << " decision=" << decision);
}

// 做完一些检查工作后, 准备发data包
void
Forwarder::onOutgoingData(const Data& data, const FaceEndpoint& egress)
{
    if (egress.face.getId() == face::INVALID_FACEID) { // 输出端口无效?
        NFD_LOG_WARN("onOutgoingData out=(invalid) data=" << data.getName());
        return;
    }
    NFD_LOG_DEBUG("onOutgoingData out=" << egress << " data=" << data.getName());

    // /localhost scope control
    bool isViolatingLocalhost = egress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL
                                && scope_prefix::LOCALHOST.isPrefixOf(data.getName());
    if (isViolatingLocalhost) {
        NFD_LOG_DEBUG("onOutgoingData out=" << egress << " data=" << data.getName()
                                            << " violates /localhost");
        // (drop)
        return;
    }

    // TODO: 官方todo,计划下个版本添加流量管理 traffic manager

    // send Data
    egress.face.sendData(data,
                         egress.endpoint); // TODO: 这个sendData才是最复杂的! 需要调用底层的ns-3实现
    ++m_counters.nOutData;
}

void
Forwarder::onIncomingNack(const FaceEndpoint& ingress, const lp::Nack& nack)
{
    // receive Nack
    // 给nack包打上IncomingFaceId标签
    nack.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
    ++m_counters.nInNacks;

    // if multi-access or ad hoc face, drop
    // 首先, 如果传入接口不是点对点的, 那么直接丢弃(因为Nack只在点对点链路上被定义)
    // 如果不是点对点通信，那么根本都没定义Nack，直接扔掉
    if (ingress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
        NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                                           << "~" << nack.getReason()
                                           << " link-type=" << ingress.face.getLinkType());
        return;
    }

    // PIT match
    shared_ptr<pit::Entry> pitEntry = m_pit.find(nack.getInterest());
    // if no PIT entry found, drop
    // 如果PIT查不到--->直接扔掉
    if (pitEntry == nullptr) {
        NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                                           << "~" << nack.getReason() << " no-PIT-entry");
        return;
    }

    // has out-record?
    auto outRecord = pitEntry->getOutRecord(ingress.face);
    // if no out-record found, drop
    // 如果out-record查不到--->扔掉
    if (outRecord == pitEntry->out_end()) {
        NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                                           << "~" << nack.getReason() << " no-out-record");
        return;
    }

    // if out-record has different Nonce, drop
    // 和out-record的最后一个记录的随机数不同--->扔掉
    if (nack.getInterest().getNonce() != outRecord->getLastNonce()) {
        NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                                           << "~" << nack.getReason() << " wrong-Nonce "
                                           << nack.getInterest().getNonce()
                                           << "!=" << outRecord->getLastNonce());
        return;
    }

    NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName() << "~"
                                       << nack.getReason() << " OK");

    // record Nack on out-record
    // 标记out-record为Nack
    outRecord->setIncomingNack(nack);

    // set PIT expiry timer to now when all out-record receive Nack
    // 如果所有out-record都Nack了，将到期时间设置为现在
    if (!fw::hasPendingOutRecords(*pitEntry)) {
        this->setExpiryTimer(pitEntry, 0_ms);
    }

    // trigger strategy: after receive NACK
    // 执行策略afterReceiveNack
    this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
        strategy.afterReceiveNack(ingress, nack, pitEntry);
    });
}

// TODO: NACK的作用? 什么情况下会发送?
// 如果Outgoing时Nack了, 则执行该函数???
void
Forwarder::onOutgoingNack(const shared_ptr<pit::Entry>& pitEntry, const FaceEndpoint& egress,
                          const lp::NackHeader& nack)
{
    if (egress.face.getId() == face::INVALID_FACEID) {
        NFD_LOG_WARN("onOutgoingNack out=(invalid)"
                     << " nack=" << pitEntry->getInterest().getName() << "~" << nack.getReason());
        return;
    }

    // has in-record?
    // 检查in-record
    auto inRecord = pitEntry->getInRecord(egress.face);

    // if no in-record found, drop
    // 如果找不到in-record，就丢掉 (因为都不知道哪个interest的问题)
    if (inRecord == pitEntry->in_end()) {
        NFD_LOG_DEBUG("onOutgoingNack out=" << egress
                                            << " nack=" << pitEntry->getInterest().getName() << "~"
                                            << nack.getReason() << " no-in-record");
        return;
    }

    // if multi-access or ad hoc face, drop
    // 如果不是点对点通信, 那么根本都没定义Nack, 直接扔掉
    if (egress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
        NFD_LOG_DEBUG("onOutgoingNack out="
                      << egress << " nack=" << pitEntry->getInterest().getName() << "~"
                      << nack.getReason() << " link-type=" << egress.face.getLinkType());
        return;
    }

    NFD_LOG_DEBUG("onOutgoingNack out=" << egress << " nack=" << pitEntry->getInterest().getName()
                                        << "~" << nack.getReason() << " OK");

    // create Nack packet with the Interest from in-record
    // 准备好Nack包
    lp::Nack nackPkt(inRecord->getInterest());
    nackPkt.setHeader(nack);

    // erase in-record
    // 擦掉in-record
    pitEntry->deleteInRecord(egress.face);

    // send Nack on face
    // 给interest发Nack
    egress.face.sendNack(nackPkt, egress.endpoint);
    ++m_counters.nOutNacks;
}

void
Forwarder::onDroppedInterest(const FaceEndpoint& egress, const Interest& interest)
{
    m_strategyChoice.findEffectiveStrategy(interest.getName()).onDroppedInterest(egress, interest);
}

void
Forwarder::onNewNextHop(const Name& prefix, const fib::NextHop& nextHop)
{
    const auto affectedEntries =
      this->getNameTree()
        .partialEnumerate(prefix, [&](const name_tree::Entry& nte) -> std::pair<bool, bool> {
            const fib::Entry* fibEntry = nte.getFibEntry();
            const fw::Strategy* strategy = nullptr;
            if (nte.getStrategyChoiceEntry() != nullptr) {
                strategy = &nte.getStrategyChoiceEntry()->getStrategy();
            }
            // current nte has buffered Interests but no fibEntry (except for the root nte) and the
            // strategy enables new nexthop behavior, we enumerate the current nte and keep visiting
            // its children.
            if (nte.getName().size() == 0
                || (strategy != nullptr && strategy->wantNewNextHopTrigger() && fibEntry == nullptr
                    && nte.hasPitEntries())) {
                return {true, true};
            }
            // we don't need the current nte (no pitEntry or strategy doesn't support new nexthop),
            // but if the current nte has no fibEntry, it's still possible that its children are
            // affected by the new nexthop.
            else if (fibEntry == nullptr) {
                return {false, true};
            }
            // if the current nte has a fibEntry, we ignore the current nte and don't visit its
            // children because they are already covered by the current nte's fibEntry.
            else {
                return {false, false};
            }
        });

    for (const auto& nte : affectedEntries) {
        for (const auto& pitEntry : nte.getPitEntries()) {
            this->dispatchToStrategy(*pitEntry, [&](fw::Strategy& strategy) {
                strategy.afterNewNextHop(nextHop, pitEntry);
            });
        }
    }
}

void
Forwarder::setExpiryTimer(const shared_ptr<pit::Entry>& pitEntry, time::milliseconds duration)
{
    BOOST_ASSERT(pitEntry);
    BOOST_ASSERT(duration >= 0_ms);

    pitEntry->expiryTimer.cancel();
    pitEntry->expiryTimer =
      getScheduler().schedule(duration, [=] { onInterestFinalize(pitEntry); });
}

void
Forwarder::insertDeadNonceList(pit::Entry& pitEntry, Face* upstream)
{
    // need Dead Nonce List insert?
    bool needDnl = true;
    if (pitEntry.isSatisfied) {
        BOOST_ASSERT(pitEntry.dataFreshnessPeriod >= 0_ms);
        needDnl = static_cast<bool>(pitEntry.getInterest().getMustBeFresh())
                  && pitEntry.dataFreshnessPeriod < m_deadNonceList.getLifetime();
    }

    if (!needDnl) {
        return;
    }

    // Dead Nonce List insert
    if (upstream == nullptr) {
        // insert all outgoing Nonces
        const auto& outRecords = pitEntry.getOutRecords();
        std::for_each(outRecords.begin(), outRecords.end(), [&](const auto& outRecord) {
            m_deadNonceList.add(pitEntry.getName(), outRecord.getLastNonce());
        });
    }
    else {
        // insert outgoing Nonce of a specific face
        auto outRecord = pitEntry.getOutRecord(*upstream);
        if (outRecord != pitEntry.getOutRecords().end()) {
            m_deadNonceList.add(pitEntry.getName(), outRecord->getLastNonce());
        }
    }
}

} // namespace nfd
