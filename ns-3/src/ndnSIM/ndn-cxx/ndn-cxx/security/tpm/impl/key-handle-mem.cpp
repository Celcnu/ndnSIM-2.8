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

#include "ndn-cxx/security/tpm/impl/key-handle-mem.hpp"
#include "ndn-cxx/security/transform/bool-sink.hpp"
#include "ndn-cxx/security/transform/buffer-source.hpp"
#include "ndn-cxx/security/transform/private-key.hpp"
#include "ndn-cxx/security/transform/signer-filter.hpp"
#include "ndn-cxx/security/transform/stream-sink.hpp"
#include "ndn-cxx/security/transform/verifier-filter.hpp"
#include "ndn-cxx/encoding/buffer-stream.hpp"

namespace ndn {
namespace security {
namespace tpm {

KeyHandleMem::KeyHandleMem(shared_ptr<transform::PrivateKey> key)
  : m_key(std::move(key))
{
    BOOST_ASSERT(m_key != nullptr);
}

ConstBufferPtr
KeyHandleMem::doSign(DigestAlgorithm digestAlgorithm, const uint8_t* buf, size_t size) const
{
    using namespace transform;

    OBufferStream sigOs;
    bufferSource(buf, size) >> signerFilter(digestAlgorithm, *m_key) >> streamSink(sigOs);
    return sigOs.buf();
}

bool
KeyHandleMem::doVerify(DigestAlgorithm digestAlgorithm, const uint8_t* buf, size_t size, const uint8_t* sig,
                       size_t sigLen) const
{
    using namespace transform;

    bool result = false;
    bufferSource(buf, size) >> verifierFilter(digestAlgorithm, *m_key, sig, sigLen) >> boolSink(result);
    return result;
}

ConstBufferPtr
KeyHandleMem::doDecrypt(const uint8_t* cipherText, size_t cipherTextLen) const
{
    return m_key->decrypt(cipherText, cipherTextLen);
}

ConstBufferPtr
KeyHandleMem::doDerivePublicKey() const
{
    return m_key->derivePublicKey();
}

} // namespace tpm
} // namespace security
} // namespace ndn
