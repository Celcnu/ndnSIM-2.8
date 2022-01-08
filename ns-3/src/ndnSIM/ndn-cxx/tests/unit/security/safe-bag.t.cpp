/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2018 Regents of the University of California.
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
 *
 * @author Zhiyi Zhang <dreamerbarrychang@gmail.com>
 */

#include "ndn-cxx/security/safe-bag.hpp"

#include "tests/boost-test.hpp"

namespace ndn {
namespace security {
namespace tests {

BOOST_AUTO_TEST_SUITE(Security)
BOOST_AUTO_TEST_SUITE(TestSafeBag)

const uint8_t CERT[] = {0x06, 0xc8, // Data
                        0x07, 0x14, // Name
                        0x08, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x08, 0x03, 0x6e, 0x64, 0x6e, 0x08, 0x06, 0x70, 0x72,
                        0x65, 0x66, 0x69, 0x78, 0x14, 0x07,                         // MetaInfo
                        0x18, 0x01,                                                 // ContentType
                        0x02, 0x19, 0x02,                                           // FreshnessPeriod
                        0x27, 0x10, 0x15, 0x08,                                     // Content
                        0x53, 0x55, 0x43, 0x43, 0x45, 0x53, 0x53, 0x21, 0x16, 0x1b, // SignatureInfo
                        0x1b, 0x01,                                                 // SignatureType
                        0x01, 0x1c, 0x16,                                           // KeyLocator
                        0x07, 0x14,                                                 // Name
                        0x08, 0x04, 0x74, 0x65, 0x73, 0x74, 0x08, 0x03, 0x6b, 0x65, 0x79, 0x08, 0x07, 0x6c, 0x6f, 0x63,
                        0x61, 0x74, 0x6f, 0x72, 0x17, 0x80, // SignatureValue
                        0x2f, 0xd6, 0xf1, 0x6e, 0x80, 0x6f, 0x10, 0xbe, 0xb1, 0x6f, 0x3e, 0x31, 0xec, 0xe3, 0xb9, 0xea,
                        0x83, 0x30, 0x40, 0x03, 0xfc, 0xa0, 0x13, 0xd9, 0xb3, 0xc6, 0x25, 0x16, 0x2d, 0xa6, 0x58, 0x41,
                        0x69, 0x62, 0x56, 0xd8, 0xb3, 0x6a, 0x38, 0x76, 0x56, 0xea, 0x61, 0xb2, 0x32, 0x70, 0x1c, 0xb6,
                        0x4d, 0x10, 0x1d, 0xdc, 0x92, 0x8e, 0x52, 0xa5, 0x8a, 0x1d, 0xd9, 0x96, 0x5e, 0xc0, 0x62, 0x0b,
                        0xcf, 0x3a, 0x9d, 0x7f, 0xca, 0xbe, 0xa1, 0x41, 0x71, 0x85, 0x7a, 0x8b, 0x5d, 0xa9, 0x64, 0xd6,
                        0x66, 0xb4, 0xe9, 0x8d, 0x0c, 0x28, 0x43, 0xee, 0xa6, 0x64, 0xe8, 0x55, 0xf6, 0x1c, 0x19, 0x0b,
                        0xef, 0x99, 0x25, 0x1e, 0xdc, 0x78, 0xb3, 0xa7, 0xaa, 0x0d, 0x14, 0x58, 0x30, 0xe5, 0x37, 0x6a,
                        0x6d, 0xdb, 0x56, 0xac, 0xa3, 0xfc, 0x90, 0x7a, 0xb8, 0x66, 0x9c, 0x0e, 0xf6, 0xb7, 0x64, 0xd1};

const uint8_t ENCRYPTED_KEY_BAG[] = {0x2f, 0xd6, 0xf1, 0x6e, 0x80, 0x6f, 0x10, 0xbe};

const uint8_t SAFE_BAG[] = {0x80, 0xd4, // SafeBag
                            0x06, 0xc8, // Data
                            0x07, 0x14, // Name
                            0x08, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x08, 0x03, 0x6e, 0x64, 0x6e, 0x08, 0x06, 0x70,
                            0x72, 0x65, 0x66, 0x69, 0x78, 0x14, 0x07,                   // MetaInfo
                            0x18, 0x01,                                                 // ContentType
                            0x02, 0x19, 0x02,                                           // FreshnessPeriod
                            0x27, 0x10, 0x15, 0x08,                                     // Content
                            0x53, 0x55, 0x43, 0x43, 0x45, 0x53, 0x53, 0x21, 0x16, 0x1b, // SignatureInfo
                            0x1b, 0x01,                                                 // SignatureType
                            0x01, 0x1c, 0x16,                                           // KeyLocator
                            0x07, 0x14,                                                 // Name
                            0x08, 0x04, 0x74, 0x65, 0x73, 0x74, 0x08, 0x03, 0x6b, 0x65, 0x79, 0x08, 0x07, 0x6c, 0x6f,
                            0x63, 0x61, 0x74, 0x6f, 0x72, 0x17, 0x80, // SignatureValue
                            0x2f, 0xd6, 0xf1, 0x6e, 0x80, 0x6f, 0x10, 0xbe, 0xb1, 0x6f, 0x3e, 0x31, 0xec, 0xe3, 0xb9,
                            0xea, 0x83, 0x30, 0x40, 0x03, 0xfc, 0xa0, 0x13, 0xd9, 0xb3, 0xc6, 0x25, 0x16, 0x2d, 0xa6,
                            0x58, 0x41, 0x69, 0x62, 0x56, 0xd8, 0xb3, 0x6a, 0x38, 0x76, 0x56, 0xea, 0x61, 0xb2, 0x32,
                            0x70, 0x1c, 0xb6, 0x4d, 0x10, 0x1d, 0xdc, 0x92, 0x8e, 0x52, 0xa5, 0x8a, 0x1d, 0xd9, 0x96,
                            0x5e, 0xc0, 0x62, 0x0b, 0xcf, 0x3a, 0x9d, 0x7f, 0xca, 0xbe, 0xa1, 0x41, 0x71, 0x85, 0x7a,
                            0x8b, 0x5d, 0xa9, 0x64, 0xd6, 0x66, 0xb4, 0xe9, 0x8d, 0x0c, 0x28, 0x43, 0xee, 0xa6, 0x64,
                            0xe8, 0x55, 0xf6, 0x1c, 0x19, 0x0b, 0xef, 0x99, 0x25, 0x1e, 0xdc, 0x78, 0xb3, 0xa7, 0xaa,
                            0x0d, 0x14, 0x58, 0x30, 0xe5, 0x37, 0x6a, 0x6d, 0xdb, 0x56, 0xac, 0xa3, 0xfc, 0x90, 0x7a,
                            0xb8, 0x66, 0x9c, 0x0e, 0xf6, 0xb7, 0x64, 0xd1, 0x81, 0x08, // EncryptedKeyBag
                            0x2f, 0xd6, 0xf1, 0x6e, 0x80, 0x6f, 0x10, 0xbe};

BOOST_AUTO_TEST_CASE(Constructor)
{
    Block dataBlock(CERT, sizeof(CERT));
    Data data(dataBlock);
    SafeBag safeBag1(data, ENCRYPTED_KEY_BAG, sizeof(ENCRYPTED_KEY_BAG));

    Block safeBagBlock(SAFE_BAG, sizeof(SAFE_BAG));
    SafeBag safeBag2(safeBagBlock);

    Buffer buffer(ENCRYPTED_KEY_BAG, sizeof(ENCRYPTED_KEY_BAG));
    SafeBag safeBag3(data, buffer);

    BOOST_CHECK(safeBag1.getCertificate() == data);
    BOOST_CHECK(safeBag1.getEncryptedKeyBag() == buffer);
    BOOST_CHECK(safeBag2.getCertificate() == data);
    BOOST_CHECK(safeBag2.getEncryptedKeyBag() == buffer);
    BOOST_CHECK(safeBag3.getCertificate() == data);
    BOOST_CHECK(safeBag3.getEncryptedKeyBag() == buffer);
}

BOOST_AUTO_TEST_CASE(EncoderAndDecoder)
{
    Block dataBlock(CERT, sizeof(CERT));
    Data data(dataBlock);
    SafeBag safeBag(data, ENCRYPTED_KEY_BAG, sizeof(ENCRYPTED_KEY_BAG));

    // wire encode
    Block wireBlock = safeBag.wireEncode();
    Block block(SAFE_BAG, sizeof(SAFE_BAG));

    // check safe bag block
    BOOST_CHECK_EQUAL(wireBlock, block);

    // wire decode
    SafeBag safeBag2;
    safeBag2.wireDecode(wireBlock);

    // check equal
    Buffer buffer1 = safeBag2.getEncryptedKeyBag();
    Buffer buffer2(ENCRYPTED_KEY_BAG, sizeof(ENCRYPTED_KEY_BAG));
    BOOST_CHECK(buffer1 == buffer2);
}

BOOST_AUTO_TEST_SUITE_END() // TestSafeBag
BOOST_AUTO_TEST_SUITE_END() // Security

} // namespace tests
} // namespace security
} // namespace ndn
