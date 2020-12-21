// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#pragma once

#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace JsonConfig {

class FieldBase;

class FieldHandler {
public:
  FieldHandler() = default;
  virtual ~FieldHandler() = default;
  void registerField(FieldBase *Ptr);
  void processConfigData(std::string const &ConfigJsonStr);
  void processConfigData(nlohmann::json const &JsonObj);

private:
  std::map<std::string, FieldBase *> FieldMap;
};

} // namespace JsonConfig
