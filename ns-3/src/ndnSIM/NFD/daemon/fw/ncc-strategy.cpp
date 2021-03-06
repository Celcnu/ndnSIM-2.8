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

#include "ncc-strategy.hpp"
#include "algorithm.hpp"
#include "common/global.hpp"

#include <ndn-cxx/util/random.hpp>

namespace nfd {
namespace fw {

NFD_REGISTER_STRATEGY(NccStrategy);

const time::microseconds NccStrategy::DEFER_FIRST_WITHOUT_BEST_FACE = 4_ms;
const time::microseconds NccStrategy::DEFER_RANGE_WITHOUT_BEST_FACE = 75_ms;
const time::nanoseconds NccStrategy::MEASUREMENTS_LIFETIME = 16_s;

NccStrategy::NccStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder)
{
    ParsedInstanceName parsed = parseInstanceName(name);
    if (!parsed.parameters.empty()) {
        NDN_THROW(std::invalid_argument("NccStrategy does not accept parameters"));
    }
    if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument("NccStrategy does not support version " + to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name&
NccStrategy::getStrategyName()
{
    static Name strategyName("/localhost/nfd/strategy/ncc/%FD%01");
    return strategyName;
}

void
NccStrategy::afterReceiveInterest(const FaceEndpoint& ingress, const Interest& interest,
                                  const shared_ptr<pit::Entry>& pitEntry)
{
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    if (nexthops.size() == 0) {
        this->rejectPendingInterest(pitEntry);
        return;
    }

    PitEntryInfo* pitEntryInfo = pitEntry->insertStrategyInfo<PitEntryInfo>().first;
    bool isNewPitEntry = !hasPendingOutRecords(*pitEntry);
    if (!isNewPitEntry) {
        return;
    }

    MeasurementsEntryInfo& meInfo = this->getMeasurementsEntryInfo(pitEntry);

    time::microseconds deferFirst = DEFER_FIRST_WITHOUT_BEST_FACE;
    time::microseconds deferRange = DEFER_RANGE_WITHOUT_BEST_FACE;
    size_t nUpstreams = nexthops.size();

    shared_ptr<Face> bestFace = meInfo.getBestFace();
    if (bestFace != nullptr && fibEntry.hasNextHop(*bestFace) && !wouldViolateScope(ingress.face, interest, *bestFace)
        && canForwardToLegacy(*pitEntry, *bestFace)) {
        // TODO Should we use `randlow = 100 + nrand48(h->seed) % 4096U;` ?
        deferFirst = meInfo.prediction;
        deferRange = time::microseconds((deferFirst.count() + 1) / 2);
        --nUpstreams;
        this->sendInterest(pitEntry, FaceEndpoint(*bestFace, 0), interest);
        pitEntryInfo->bestFaceTimeout =
          getScheduler().schedule(meInfo.prediction,
                                  bind(&NccStrategy::timeoutOnBestFace, this, weak_ptr<pit::Entry>(pitEntry)));
    }
    else {
        // use first eligible nexthop
        auto firstEligibleNexthop = std::find_if(nexthops.begin(), nexthops.end(), [&](const fib::NextHop& nexthop) {
            Face& outFace = nexthop.getFace();
            return !wouldViolateScope(ingress.face, interest, outFace) && canForwardToLegacy(*pitEntry, outFace);
        });
        if (firstEligibleNexthop != nexthops.end()) {
            this->sendInterest(pitEntry, FaceEndpoint(firstEligibleNexthop->getFace(), 0), interest);
        }
        else {
            this->rejectPendingInterest(pitEntry);
            return;
        }
    }

    shared_ptr<Face> previousFace = meInfo.previousFace.lock();
    if (previousFace != nullptr && fibEntry.hasNextHop(*previousFace)
        && !wouldViolateScope(ingress.face, interest, *previousFace) && canForwardToLegacy(*pitEntry, *previousFace)) {
        --nUpstreams;
    }

    if (nUpstreams > 0) {
        pitEntryInfo->maxInterval =
          std::max(1_us, time::microseconds((2 * deferRange.count() + nUpstreams - 1) / nUpstreams));
    }
    else {
        // Normally, maxInterval is unused if there aren't any face beyond best and previousBest.
        // However, in case FIB entry gains a new nexthop before doPropagate executes (bug 1853),
        // this maxInterval would be used to determine when the next doPropagate would happen.
        pitEntryInfo->maxInterval = deferFirst;
    }
    pitEntryInfo->propagateTimer =
      getScheduler().schedule(deferFirst, bind(&NccStrategy::doPropagate, this, ingress.face.getId(),
                                               weak_ptr<pit::Entry>(pitEntry)));
}

void
NccStrategy::doPropagate(FaceId inFaceId, weak_ptr<pit::Entry> pitEntryWeak)
{
    Face* inFace = this->getFace(inFaceId);
    if (inFace == nullptr) {
        return;
    }
    shared_ptr<pit::Entry> pitEntry = pitEntryWeak.lock();
    if (pitEntry == nullptr) {
        return;
    }
    auto inRecord = pitEntry->getInRecord(*inFace);
    if (inRecord == pitEntry->in_end()) {
        return;
    }
    const Interest& interest = inRecord->getInterest();
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);

    PitEntryInfo* pitEntryInfo = pitEntry->getStrategyInfo<PitEntryInfo>();
    // pitEntryInfo is guaranteed to exist here, because doPropagate is triggered
    // from a timer set by NccStrategy.
    BOOST_ASSERT(pitEntryInfo != nullptr);

    MeasurementsEntryInfo& meInfo = this->getMeasurementsEntryInfo(pitEntry);

    shared_ptr<Face> previousFace = meInfo.previousFace.lock();
    if (previousFace != nullptr && fibEntry.hasNextHop(*previousFace)
        && !wouldViolateScope(*inFace, interest, *previousFace) && canForwardToLegacy(*pitEntry, *previousFace)) {
        this->sendInterest(pitEntry, FaceEndpoint(*previousFace, 0), interest);
    }

    bool isForwarded = false;
    for (const auto& nexthop : fibEntry.getNextHops()) {
        Face& face = nexthop.getFace();
        if (!wouldViolateScope(*inFace, interest, face) && canForwardToLegacy(*pitEntry, face)) {
            isForwarded = true;
            this->sendInterest(pitEntry, FaceEndpoint(face, 0), interest);
            break;
        }
    }

    if (isForwarded) {
        std::uniform_int_distribution<time::nanoseconds::rep> dist(0, pitEntryInfo->maxInterval.count() - 1);
        time::nanoseconds deferNext(dist(ndn::random::getRandomNumberEngine()));
        pitEntryInfo->propagateTimer =
          getScheduler().schedule(deferNext,
                                  bind(&NccStrategy::doPropagate, this, inFaceId, weak_ptr<pit::Entry>(pitEntry)));
    }
}

void
NccStrategy::timeoutOnBestFace(weak_ptr<pit::Entry> pitEntryWeak)
{
    shared_ptr<pit::Entry> pitEntry = pitEntryWeak.lock();
    if (pitEntry == nullptr) {
        return;
    }
    measurements::Entry* measurementsEntry = this->getMeasurements().get(*pitEntry);

    for (int i = 0; i < UPDATE_MEASUREMENTS_N_LEVELS; ++i) {
        if (measurementsEntry == nullptr) {
            // going out of this strategy's namespace
            break;
        }
        this->getMeasurements().extendLifetime(*measurementsEntry, MEASUREMENTS_LIFETIME);

        MeasurementsEntryInfo& meInfo = this->getMeasurementsEntryInfo(measurementsEntry);
        meInfo.adjustPredictUp();

        measurementsEntry = this->getMeasurements().getParent(*measurementsEntry);
    }
}

void
NccStrategy::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry, const FaceEndpoint& ingress,
                                   const Data& data)
{
    if (!pitEntry->hasInRecords()) {
        // PIT entry has already been satisfied (and is now waiting for straggler timer to expire)
        // NCC does not collect measurements for non-best face
        return;
    }

    measurements::Entry* measurementsEntry = this->getMeasurements().get(*pitEntry);

    for (int i = 0; i < UPDATE_MEASUREMENTS_N_LEVELS; ++i) {
        if (measurementsEntry == nullptr) {
            // going out of this strategy's namespace
            return;
        }
        this->getMeasurements().extendLifetime(*measurementsEntry, MEASUREMENTS_LIFETIME);

        MeasurementsEntryInfo& meInfo = this->getMeasurementsEntryInfo(measurementsEntry);
        meInfo.updateBestFace(ingress.face);

        measurementsEntry = this->getMeasurements().getParent(*measurementsEntry);
    }

    PitEntryInfo* pitEntryInfo = pitEntry->getStrategyInfo<PitEntryInfo>();
    if (pitEntryInfo != nullptr) {
        pitEntryInfo->propagateTimer.cancel();

        // Verify that the best face satisfied the interest before canceling the timeout call
        MeasurementsEntryInfo& meInfo = this->getMeasurementsEntryInfo(pitEntry);
        shared_ptr<Face> bestFace = meInfo.getBestFace();

        if (bestFace.get() == &ingress.face)
            pitEntryInfo->bestFaceTimeout.cancel();
    }
}

NccStrategy::MeasurementsEntryInfo&
NccStrategy::getMeasurementsEntryInfo(const shared_ptr<pit::Entry>& entry)
{
    measurements::Entry* measurementsEntry = this->getMeasurements().get(*entry);
    return this->getMeasurementsEntryInfo(measurementsEntry);
}

NccStrategy::MeasurementsEntryInfo&
NccStrategy::getMeasurementsEntryInfo(measurements::Entry* entry)
{
    BOOST_ASSERT(entry != nullptr);
    MeasurementsEntryInfo* info = nullptr;
    bool isNew = false;
    std::tie(info, isNew) = entry->insertStrategyInfo<MeasurementsEntryInfo>();
    if (!isNew) {
        return *info;
    }

    measurements::Entry* parentEntry = this->getMeasurements().getParent(*entry);
    if (parentEntry != nullptr) {
        MeasurementsEntryInfo& parentInfo = this->getMeasurementsEntryInfo(parentEntry);
        info->inheritFrom(parentInfo);
    }

    return *info;
}

const time::microseconds NccStrategy::MeasurementsEntryInfo::INITIAL_PREDICTION = 8192_us;
const time::microseconds NccStrategy::MeasurementsEntryInfo::MIN_PREDICTION = 127_us;
const time::microseconds NccStrategy::MeasurementsEntryInfo::MAX_PREDICTION = 160_ms;

NccStrategy::MeasurementsEntryInfo::MeasurementsEntryInfo()
  : prediction(INITIAL_PREDICTION)
{
}

void
NccStrategy::MeasurementsEntryInfo::inheritFrom(const MeasurementsEntryInfo& other)
{
    this->operator=(other);
}

shared_ptr<Face>
NccStrategy::MeasurementsEntryInfo::getBestFace()
{
    shared_ptr<Face> best = this->bestFace.lock();
    if (best != nullptr) {
        return best;
    }
    this->bestFace = best = this->previousFace.lock();
    return best;
}

void
NccStrategy::MeasurementsEntryInfo::updateBestFace(const Face& face)
{
    if (this->bestFace.expired()) {
        this->bestFace = const_cast<Face&>(face).shared_from_this();
        return;
    }
    shared_ptr<Face> bestFace = this->bestFace.lock();
    if (bestFace.get() == &face) {
        this->adjustPredictDown();
    }
    else {
        this->previousFace = this->bestFace;
        this->bestFace = const_cast<Face&>(face).shared_from_this();
    }
}

void
NccStrategy::MeasurementsEntryInfo::adjustPredictDown()
{
    prediction = std::max(MIN_PREDICTION,
                          time::microseconds(prediction.count() - (prediction.count() >> ADJUST_PREDICT_DOWN_SHIFT)));
}

void
NccStrategy::MeasurementsEntryInfo::adjustPredictUp()
{
    prediction = std::min(MAX_PREDICTION,
                          time::microseconds(prediction.count() + (prediction.count() >> ADJUST_PREDICT_UP_SHIFT)));
}

void
NccStrategy::MeasurementsEntryInfo::ageBestFace()
{
    this->previousFace = this->bestFace;
    this->bestFace.reset();
}

NccStrategy::PitEntryInfo::~PitEntryInfo()
{
    bestFaceTimeout.cancel();
    propagateTimer.cancel();
}

} // namespace fw
} // namespace nfd
