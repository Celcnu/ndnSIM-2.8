/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2019 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 */

#include "ndn-cxx/encoding/block-helpers.hpp"

#include <boost/endian/conversion.hpp>

namespace ndn {
namespace encoding {

namespace endian = boost::endian;

// ---- non-negative integer ----

template <Tag TAG>
size_t
prependNonNegativeIntegerBlock(EncodingImpl<TAG>& encoder, uint32_t type, uint64_t value)
{
    size_t valueLength = encoder.prependNonNegativeInteger(value);
    size_t totalLength = valueLength;
    totalLength += encoder.prependVarNumber(valueLength);
    totalLength += encoder.prependVarNumber(type);

    return totalLength;
}

template size_t prependNonNegativeIntegerBlock<EstimatorTag>(EncodingImpl<EstimatorTag>&, uint32_t, uint64_t);

template size_t prependNonNegativeIntegerBlock<EncoderTag>(EncodingImpl<EncoderTag>&, uint32_t, uint64_t);

Block
makeNonNegativeIntegerBlock(uint32_t type, uint64_t value)
{
    EncodingEstimator estimator;
    size_t totalLength = prependNonNegativeIntegerBlock(estimator, type, value);

    EncodingBuffer encoder(totalLength, 0);
    prependNonNegativeIntegerBlock(encoder, type, value);

    return encoder.block();
}

uint64_t
readNonNegativeInteger(const Block& block)
{
    auto begin = block.value_begin();
    return tlv::readNonNegativeInteger(block.value_size(), begin, block.value_end());
}

// ---- empty ----

template <Tag TAG>
size_t
prependEmptyBlock(EncodingImpl<TAG>& encoder, uint32_t type)
{
    size_t totalLength = encoder.prependVarNumber(0);
    totalLength += encoder.prependVarNumber(type);

    return totalLength;
}

template size_t prependEmptyBlock<EstimatorTag>(EncodingImpl<EstimatorTag>&, uint32_t);

template size_t prependEmptyBlock<EncoderTag>(EncodingImpl<EncoderTag>&, uint32_t);

Block
makeEmptyBlock(uint32_t type)
{
    EncodingEstimator estimator;
    size_t totalLength = prependEmptyBlock(estimator, type);

    EncodingBuffer encoder(totalLength, 0);
    prependEmptyBlock(encoder, type);

    return encoder.block();
}

// ---- string ----

template <Tag TAG>
size_t
prependStringBlock(EncodingImpl<TAG>& encoder, uint32_t type, const std::string& value)
{
    return encoder.prependByteArrayBlock(type, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

template size_t prependStringBlock<EstimatorTag>(EncodingImpl<EstimatorTag>&, uint32_t, const std::string&);

template size_t prependStringBlock<EncoderTag>(EncodingImpl<EncoderTag>&, uint32_t, const std::string&);

Block
makeStringBlock(uint32_t type, const std::string& value)
{
    return makeBinaryBlock(type, value.data(), value.size());
}

std::string
readString(const Block& block)
{
    return std::string(reinterpret_cast<const char*>(block.value()), block.value_size());
}

// ---- double ----

static_assert(std::numeric_limits<double>::is_iec559, "This code requires IEEE-754 doubles");

template <Tag TAG>
size_t
prependDoubleBlock(EncodingImpl<TAG>& encoder, uint32_t type, double value)
{
    uint64_t temp = 0;
    std::memcpy(&temp, &value, 8);
    endian::native_to_big_inplace(temp);
    return encoder.prependByteArrayBlock(type, reinterpret_cast<const uint8_t*>(&temp), 8);
}

template size_t prependDoubleBlock<EstimatorTag>(EncodingImpl<EstimatorTag>&, uint32_t, double);

template size_t prependDoubleBlock<EncoderTag>(EncodingImpl<EncoderTag>&, uint32_t, double);

Block
makeDoubleBlock(uint32_t type, double value)
{
    EncodingEstimator estimator;
    size_t totalLength = prependDoubleBlock(estimator, type, value);

    EncodingBuffer encoder(totalLength, 0);
    prependDoubleBlock(encoder, type, value);

    return encoder.block();
}

double
readDouble(const Block& block)
{
    if (block.value_size() != 8) {
        NDN_THROW(tlv::Error("Invalid length for double (must be 8)"));
    }

#if BOOST_VERSION >= 107100
    return endian::endian_load<double, 8, endian::order::big>(block.value());
#else
    uint64_t temp = 0;
    std::memcpy(&temp, block.value(), 8);
    endian::big_to_native_inplace(temp);
    double d = 0;
    std::memcpy(&d, &temp, 8);
    return d;
#endif
}

// ---- binary ----

Block
makeBinaryBlock(uint32_t type, const uint8_t* value, size_t length)
{
    EncodingEstimator estimator;
    size_t totalLength = estimator.prependByteArrayBlock(type, value, length);

    EncodingBuffer encoder(totalLength, 0);
    encoder.prependByteArrayBlock(type, value, length);

    return encoder.block();
}

Block
makeBinaryBlock(uint32_t type, const char* value, size_t length)
{
    return makeBinaryBlock(type, reinterpret_cast<const uint8_t*>(value), length);
}

} // namespace encoding
} // namespace ndn
