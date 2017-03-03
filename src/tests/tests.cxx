#include <gtest/gtest.h>
#include "../MainOpt.h"
#include "roundtrip.h"

int main(int argc, char ** argv) {
	auto po = parse_opt(argc, argv);
	if (po.first) {
		return 1;
	}
	auto opt = std::move(po.second);
	setup_logger_from_options(*opt);
	Roundtrip::opt = opt.get();
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

static_assert(RD_KAFKA_RESP_ERR_NO_ERROR == 0, "Make sure that NO_ERROR is and stays 0");

TEST_F(Roundtrip, simple_01) {
	// disabled
	return;
	BrightnESS::FileWriter::Test::roundtrip_simple_01(*opt);
}

#if 0
	if (false) {
		// test if log messages arrive on all destinations
		for (int i1 = 0; i1 < 100; ++i1) {
			LOG(i1 % 8, "Log ix {} level {}", i1, i1 % 8);
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	}
#endif
