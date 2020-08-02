// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#pragma once

#include <variant>

struct MainOpt;

namespace FileWriter {

namespace States {
struct Idle {};

struct StartRequested {
  StartCommandInfo StartInfo;
};

struct Writing {};

struct StopRequested {
  StopCommandInfo StopInfo;
};
} // namespace States

using FileWriterState = std::variant<States::Idle, States::StartRequested,
                                     States::Writing, States::StopRequested>;

} // namespace FileWriter
