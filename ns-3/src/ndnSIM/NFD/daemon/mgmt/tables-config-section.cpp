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

#include "tables-config-section.hpp"
#include "fw/strategy.hpp"

namespace nfd {

const size_t TablesConfigSection::DEFAULT_CS_MAX_PACKETS = 65536;

TablesConfigSection::TablesConfigSection(Forwarder& forwarder)
  : m_forwarder(forwarder)
  , m_isConfigured(false)
{
}

void
TablesConfigSection::setConfigFile(ConfigFile& configFile)
{
	std::cout << "TablesConfigSection::setConfigFile()" << std::endl;
	// 这里直接会执行 processConfig ? 不是, 是通过ConfigFile::process()
    // 这里是把tables相关的配置绑定到processConfig来处理,在ConfigFile::process()里会用到
	configFile.addSectionHandler("tables", bind(&TablesConfigSection::processConfig, this, _1, _2));
}

void
TablesConfigSection::ensureConfigured()
{
    if (m_isConfigured) {
        return;
    }

    m_forwarder.getCs().setLimit(DEFAULT_CS_MAX_PACKETS);
    // Don't set default cs_policy because it's already created by CS itself.
    m_forwarder.setUnsolicitedDataPolicy(make_unique<fw::DefaultUnsolicitedDataPolicy>());

    m_isConfigured = true;
}

void
TablesConfigSection::processConfig(const ConfigSection& section, bool isDryRun)
{
	// std::cout << "TablesConfigSection::processConfig()" << std::endl;

	// 最终是在这里实现具体的设置!
    size_t nCsMaxPackets = DEFAULT_CS_MAX_PACKETS;
    OptionalConfigSection csMaxPacketsNode = section.get_child_optional("cs_max_packets"); // 拿到你设置的缓存容量
    if (csMaxPacketsNode) {
		// std::cout << "\tread CS config..." << std::endl;
        nCsMaxPackets = ConfigFile::parseNumber<size_t>(*csMaxPacketsNode, "cs_max_packets", "tables"); // 从前面我们设置的配置文件中读对应值
    }

    unique_ptr<cs::Policy> csPolicy;
    OptionalConfigSection csPolicyNode = section.get_child_optional("cs_policy"); // 拿到你设置的替换策略
    if (csPolicyNode) {
        std::string policyName = csPolicyNode->get_value<std::string>();
        csPolicy = cs::Policy::create(policyName);
        if (csPolicy == nullptr) {
            NDN_THROW(ConfigFile::Error("Unknown cs_policy '" + policyName + "' in section 'tables'"));
        }
    }

    unique_ptr<fw::UnsolicitedDataPolicy> unsolicitedDataPolicy;
    OptionalConfigSection unsolicitedDataPolicyNode = section.get_child_optional("cs_unsolicited_policy");
    if (unsolicitedDataPolicyNode) {
        std::string policyName = unsolicitedDataPolicyNode->get_value<std::string>();
        unsolicitedDataPolicy = fw::UnsolicitedDataPolicy::create(policyName);
        if (unsolicitedDataPolicy == nullptr) {
            NDN_THROW(ConfigFile::Error("Unknown cs_unsolicited_policy '" + policyName + "' in section 'tables'"));
        }
    } else {
        unsolicitedDataPolicy = make_unique<fw::DefaultUnsolicitedDataPolicy>();
    }

    OptionalConfigSection strategyChoiceSection = section.get_child_optional("strategy_choice");
    if (strategyChoiceSection) {
        processStrategyChoiceSection(*strategyChoiceSection, isDryRun);
    }

    OptionalConfigSection networkRegionSection = section.get_child_optional("network_region");
    if (networkRegionSection) {
        processNetworkRegionSection(*networkRegionSection, isDryRun);
    }

    if (isDryRun) {
        return;
    }

	// std::cout << "\tset CS config..." << std::endl;
    Cs& cs = m_forwarder.getCs();
    cs.setLimit(nCsMaxPackets);
    if (cs.size() == 0 && csPolicy != nullptr) { 
		// size 是当前缓存的条目数
		// 即设置策略只能在缓存为空的时候进行
        cs.setPolicy(std::move(csPolicy));
    }

    m_forwarder.setUnsolicitedDataPolicy(std::move(unsolicitedDataPolicy));

    m_isConfigured = true;
}

void
TablesConfigSection::processStrategyChoiceSection(const ConfigSection& section, bool isDryRun)
{
    using fw::Strategy;

    std::map<Name, Name> choices;
    for (const auto& prefixAndStrategy : section) {
        Name prefix(prefixAndStrategy.first);
        Name strategy(prefixAndStrategy.second.get_value<std::string>());

        if (!Strategy::canCreate(strategy)) {
            NDN_THROW(ConfigFile::Error("Unknown strategy '" + prefixAndStrategy.second.get_value<std::string>()
                                        + "' for prefix '" + prefix.toUri() + "' in section 'strategy_choice'"));
        }

        if (!choices.emplace(prefix, strategy).second) {
            NDN_THROW(ConfigFile::Error("Duplicate strategy choice for prefix '" + prefix.toUri()
                                        + "' in section 'strategy_choice'"));
        }
    }

    if (isDryRun) {
        return;
    }

    StrategyChoice& sc = m_forwarder.getStrategyChoice();
    for (const auto& prefixAndStrategy : choices) {
        if (!sc.insert(prefixAndStrategy.first, prefixAndStrategy.second)) {
            NDN_THROW(ConfigFile::Error("Failed to set strategy '" + prefixAndStrategy.second.toUri() + "' for prefix '"
                                        + prefixAndStrategy.first.toUri() + "' in section 'strategy_choice'"));
        }
    }
    ///\todo redesign so that strategy parameter errors can be catched during dry-run
}

void
TablesConfigSection::processNetworkRegionSection(const ConfigSection& section, bool isDryRun)
{
    if (isDryRun) {
        return;
    }

    auto& nrt = m_forwarder.getNetworkRegionTable();
    nrt.clear();
    for (const auto& pair : section) {
        nrt.insert(Name(pair.first));
    }
}

} // namespace nfd
