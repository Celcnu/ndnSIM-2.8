/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2018,  Regents of the University of California,
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

#ifndef NFD_DAEMON_FACE_UNIX_STREAM_CHANNEL_HPP
#define NFD_DAEMON_FACE_UNIX_STREAM_CHANNEL_HPP

#include "channel.hpp"

namespace nfd {

namespace unix_stream {
typedef boost::asio::local::stream_protocol::endpoint Endpoint;
} // namespace unix_stream

namespace face {

/**
 * \brief Class implementing a local channel to create faces
 *
 * Channel can create faces as a response to incoming IPC connections
 * (UnixStreamChannel::listen needs to be called for that to work).
 */
class UnixStreamChannel : public Channel {
  public:
    /**
     * \brief UnixStreamChannel-related error
     */
    class Error : public std::runtime_error {
      public:
        explicit Error(const std::string& what)
          : std::runtime_error(what)
        {
        }
    };

    /**
     * \brief Create UnixStream channel for the specified endpoint
     *
     * To enable creation of faces upon incoming connections, one
     * needs to explicitly call UnixStreamChannel::listen method.
     */
    UnixStreamChannel(const unix_stream::Endpoint& endpoint, bool wantCongestionMarking);

    ~UnixStreamChannel() override;

    bool
    isListening() const override
    {
        return m_acceptor.is_open();
    }

    size_t
    size() const override
    {
        return m_size;
    }

    /**
     * \brief Start listening
     *
     * Enable listening on the Unix socket, waiting for incoming connections,
     * and creating a face when a connection is made.
     *
     * Faces created in this way will have on-demand persistency.
     *
     * \param onFaceCreated  Callback to notify successful creation of the face
     * \param onAcceptFailed Callback to notify when channel fails (accept call
     *                       returns an error)
     * \param backlog        The maximum length of the queue of pending incoming
     *                       connections
     * \throw Error
     */
    void listen(const FaceCreatedCallback& onFaceCreated, const FaceCreationFailedCallback& onAcceptFailed,
                int backlog = boost::asio::local::stream_protocol::acceptor::max_connections);

  private:
    void accept(const FaceCreatedCallback& onFaceCreated, const FaceCreationFailedCallback& onAcceptFailed);

    void handleAccept(const boost::system::error_code& error, const FaceCreatedCallback& onFaceCreated,
                      const FaceCreationFailedCallback& onAcceptFailed);

  private:
    const unix_stream::Endpoint m_endpoint;
    boost::asio::local::stream_protocol::acceptor m_acceptor;
    boost::asio::local::stream_protocol::socket m_socket;
    size_t m_size;
    bool m_wantCongestionMarking;
};

} // namespace face
} // namespace nfd

#endif // NFD_DAEMON_FACE_UNIX_STREAM_CHANNEL_HPP
