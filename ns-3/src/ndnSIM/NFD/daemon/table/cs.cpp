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

#include "cs.hpp"
#include "common/logger.hpp"
#include "core/algorithm.hpp"

#include <ndn-cxx/lp/tags.hpp>
#include <ndn-cxx/util/concepts.hpp>

namespace nfd {
namespace cs {

NFD_LOG_INIT(ContentStore);

static unique_ptr<Policy>
makeDefaultPolicy()
{
    return Policy::create("lru");
}

// who called this constructor ?  Forwarder's constructor
Cs::Cs(size_t nMaxPackets)
{
	// std::cout << "Cs::Cs()  --> default " << nMaxPackets << std::endl;
    setPolicyImpl(makeDefaultPolicy());
    m_policy->setLimit(nMaxPackets);
}


bool
Cs::insert(const Data& data, bool isUnsolicited)
{
	// NFD_LOG_DEBUG("insert " << data.getName());

    if (!m_shouldAdmit || m_policy->getLimit() == 0) {
        return false;
    }
    
    // 尝试取出data的CachePolicyTag: 如果data没Tag, 视为可以缓存; 如果tag指明不缓存 直接return
    shared_ptr<lp::CachePolicyTag> tag = data.getTag<lp::CachePolicyTag>();
    if (tag != nullptr) {
        lp::CachePolicyType policy = tag->get().getPolicy();
        if (policy == lp::CachePolicyType::NO_CACHE) {
            return false; 
        }
    }

	// cache decision
	bool cacheDecRes = cacheDecisionImpl(data);
	if (!cacheDecRes) 
		return false;

	// 后面的emplace, 这才是真正的缓存插入!
	NFD_LOG_DEBUG("insert " << data.getName());

	const_iterator it;
    bool isNewEntry = false;
    std::tie(it, isNewEntry) = m_table.emplace(data.shared_from_this(), isUnsolicited); 
    Entry& entry = const_cast<Entry&>(*it);
    entry.updateFreshUntil(); // fresh一下新来的这个包

    if (!isNewEntry) {
        if (entry.isUnsolicited() && !isUnsolicited) {
            entry.clearUnsolicited();
        }
        m_policy->afterRefresh(it); 
    } 
	else { // 如果是新的包 -> 插入缓存队列
		m_policy->afterInsert(it); // cyc: 和 m_table 的区别 ? 这里两个表都在维护 CS ?
	}
	return true;
}

std::pair<Cs::const_iterator, Cs::const_iterator>
Cs::findPrefixRange(const Name& prefix) const
{
    auto first = m_table.lower_bound(prefix);
    auto last = m_table.end();
    if (prefix.size() > 0) {
        last = m_table.lower_bound(prefix.getSuccessor());
    }
    return {first, last};
}

size_t
Cs::eraseImpl(const Name& prefix, size_t limit)
{
    const_iterator i, last;
    std::tie(i, last) = findPrefixRange(prefix);

    size_t nErased = 0;
    while (i != last && nErased < limit) {
        m_policy->beforeErase(i);
        i = m_table.erase(i);
        ++nErased;
    }
    return nErased;
}

Cs::const_iterator
Cs::findImpl(const Interest& interest) const
{
    if (!m_shouldServe || m_policy->getLimit() == 0) {
        return m_table.end();
    }

	// 把每个节点的所有缓存项都列出来
	// NFD_LOG_DEBUG("cs-size " << m_table.size());
	// std::set<Entry, std::less<>>::iterator it;
	// for(it = m_table.begin(); it != m_table.end(); it++) 
    // 	std::cout << "\t" << (*it).getName() << " " << std::endl;

    const Name& prefix = interest.getName();
    auto range = findPrefixRange(prefix);

	// 这里是在查 m_table
	auto match =
      std::find_if(range.first, range.second, [&interest](const auto& entry) { return entry.canSatisfy(interest); });

    // 这里好像不只在匹配兴趣??? 是的
	// 我们对打印作一些更改,设置为只打印内容相关的查询log
	std::string testStr = "/localhost/nfd/";
  	std::string interestName =prefix.toUri();
  	std::string::size_type idx = interestName.find(testStr);
  	bool printFlag = false;
  	if (idx == std::string::npos) {
		printFlag = true;
  	} 

    if (match == range.second) {
		if (printFlag)
			NFD_LOG_DEBUG("find " << prefix << " no-match");
        return m_table.end();
    }
	NFD_LOG_DEBUG("find " << prefix << " matching " << match->getName());
	// 查看此时一共缓存了多少内容项, 这个还会包含其它非内容项? 把所有insert的打印都打开
	// 把所有缓存的内容都打印出来 ?

	
	
	// 这里是匹配到data ---> 这里也不会涉及Tag的操作 ---> 这个Tag到底是哪里来的?
    m_policy->beforeUse(match); // 更新队列

    return match;
}

void
Cs::dump()
{
    NFD_LOG_DEBUG("dump table");
    for (const Entry& entry : m_table) {
        NFD_LOG_TRACE(entry.getFullName());
    }
}

void
Cs::setPolicy(unique_ptr<Policy> policy)
{
    BOOST_ASSERT(policy != nullptr);
    BOOST_ASSERT(m_policy != nullptr);
    size_t limit = m_policy->getLimit();
    this->setPolicyImpl(std::move(policy));
    m_policy->setLimit(limit);
}

void
Cs::setPolicyImpl(unique_ptr<Policy> policy)
{
	// 每个节点会调用2次??? 为啥会有两次打印 ~
	// 构造函数会调用 1 次, 后面setPolicy会删除前面构造时设置的那个
    // NFD_LOG_DEBUG("set-policy " << policy->getName());
    m_policy = std::move(policy);
    m_beforeEvictConnection = m_policy->beforeEvict.connect([this](auto it) { m_table.erase(it); });

    m_policy->setCs(this);
    BOOST_ASSERT(m_policy->getCs() == this);
}

void
Cs::enableAdmit(bool shouldAdmit)
{
    if (m_shouldAdmit == shouldAdmit) {
        return;
    }
    m_shouldAdmit = shouldAdmit;
    NFD_LOG_INFO((shouldAdmit ? "Enabling" : "Disabling") << " Data admittance");
}

void
Cs::enableServe(bool shouldServe)
{
    if (m_shouldServe == shouldServe) {
        return;
    }
    m_shouldServe = shouldServe;
    NFD_LOG_INFO((shouldServe ? "Enabling" : "Disabling") << " Data serving");
}

// chaochao added ↓

bool
Cs::cacheDecisionImpl(const Data& data)
{
	return true; // LCE
	// return cacheDecisionLCD(data); // LCD
}

bool
Cs::cacheDecisionLCD(const Data& data) 
{
	// (1) if management protocol --> not insert
	bool isData = true;
	std::string testStr = "/localhost/nfd/";
  	std::string dataName = data.getName().toUri();
  	std::string::size_type idx = dataName.find(testStr);
  	if (idx != std::string::npos) { // match ---> 即不是常规data项
		isData = false;
		// m_policy->afterInsert(it); // 直接缓存 or 全部不缓存,避免对我们的统计造成影响!
		return false;
	}

	// (2) content  
	auto chaochaoTag = data.getTag<lp::ChaoChaoTag>();  // Tag 是指针, 直接打印的输出是地址!
	int cc_tag = 0;
	if (chaochaoTag != nullptr) { 
		cc_tag = *chaochaoTag;
	} 
	// NFD_LOG_DEBUG("cc_tag: " << cc_tag); 
	if (cc_tag == 0) { // 有2种情形: (1) Producer响应数据,tag为null; (2) 缓存节点响应,*tag为0
		// m_policy->afterInsert(it); // 后面操作的都是m_queue了! 所以insert在此之前可能就完成了? 这里都是after了?
		return true;
	} else {
		return false; 
	}
}


} // namespace cs
} // namespace nfd
