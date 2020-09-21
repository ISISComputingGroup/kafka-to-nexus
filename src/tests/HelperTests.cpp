// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#include <gtest/gtest.h>
#include "helper.h"
#include <algorithm>

TEST(HelperTests, RandomHexStringGeneration) {
  size_t StringLength{200};
  auto TestString = randomHexString(StringLength);
  EXPECT_EQ(StringLength, TestString.size());
  std::string const hexChars = "0123456789abcdef";
  for (auto const &Character : TestString) {
    if (hexChars.find(Character) == std::string::npos) {
      FAIL();
    }
  }
}

TEST(HelperTests, RandomHexStringNotEqual) {
  auto StringLength{10};
  auto TestString1 = randomHexString(StringLength);
  auto TestString2 = randomHexString(StringLength);
  EXPECT_NE(TestString1, TestString2);
}

TEST(HelperTests, PidIsNotZero) {
  EXPECT_NE(getPID(), 0);
}

TEST(HelperTests, HostNameIsNotEmpty) {
  EXPECT_FALSE(getHostName().empty());
}
