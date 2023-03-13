
// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief StatusService class
///
/// Listens for connections on specified TCP port, replies with a status
/// message. Add-on for supporting the Dashboard service, but potentially also
/// useful for NICOS.
//===----------------------------------------------------------------------===//

#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>
#include <thread>

namespace FileWriter {

class StatusService {
public:
  /// \brief Service constructor.
  /// \param TcpPort Desired tcp port for accepting connections(default 8888)
  StatusService(int TcpPort);

  /// \brief launches run() in a thread.
  void startThread();

  /// \brief listen for connection, return status message, repeat.
  void run();

private:
  char TxBuffer[1025]; // holds the service status text message
  std::thread status;
  int TcpPort{8888};
  int ListenFd{0}; // File descriptor for listening
  struct sockaddr_in ServerAddr;

  static constexpr int ONE_SECOND{1};
  static constexpr int MESSAGE_BACKLOG{10}; // Max # of queued connections
};

} // namespace FileWriter
