/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/tools/xla_compile_lib.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "google/protobuf/duration.pb.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "xla/hlo/ir/hlo_module.h"
#include "xla/service/platform_util.h"
#include "xla/service/xla_compile_result.pb.h"
#include "xla/stream_executor/device_description.pb.h"
#include "xla/tests/hlo_test_base.h"
#include "xla/tests/test_macros.h"
#include "xla/util.h"
#include "tsl/lib/core/status_test_util.h"
#include "tsl/platform/env.h"
#include "tsl/platform/env_time.h"
#include "tsl/platform/path.h"
#include "tsl/platform/status_matchers.h"
#include "tsl/platform/statusor.h"
#include "tsl/platform/test.h"
#include "tsl/protobuf/error_codes.pb.h"

namespace xla {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;
using ::tsl::testing::IsOk;
using ::tsl::testing::IsOkAndHolds;
using ::tsl::testing::StatusIs;

#if XLA_TEST_BACKEND_CPU
static constexpr absl::string_view kPlatformName = "Host";
#elif XLA_TEST_BACKEND_GPU
static constexpr absl::string_view kPlatformName = "CUDA";
#endif  // XLA_TEST_BACKEND_CPU

class XlaCompileLibTest : public HloTestBase {
 protected:
  XlaCompileLibTest()
      : HloTestBase(*PlatformUtil::GetPlatform(std::string(kPlatformName)),
                    GetReferencePlatform()) {}
  void SetUp() override {
    const std::string hlo_path = tsl::io::JoinPath(tsl::testing::XlaSrcRoot(),
                                                   "tools", "data", "add.hlo");
    std::string hlo;
    TF_ASSERT_OK(tsl::ReadFileToString(tsl::Env::Default(), hlo_path, &hlo));
    TF_ASSERT_OK_AND_ASSIGN(module_, ParseAndReturnVerifiedModule(hlo));
  }

  std::unique_ptr<HloModule> module_;
};

TEST_F(XlaCompileLibTest, DISABLED_ON_GPU(CompilesForCpu)) {
  EXPECT_THAT(CompileExecutable(std::move(module_), "cpu", std::nullopt),
              IsOkAndHolds(Not(IsEmpty())));
}

TEST_F(XlaCompileLibTest, DISABLED_ON_CPU(CompilesForGpuWithDevice)) {
  EXPECT_THAT(CompileExecutable(std::move(module_), "gpu", std::nullopt),
              IsOkAndHolds(Not(IsEmpty())));
}

TEST_F(XlaCompileLibTest, DISABLED_ON_CPU(CompilesForGpuWithoutDevice)) {
  const std::string target_config_path =
      tsl::io::JoinPath(tsl::testing::XlaSrcRoot(), "service",
                        "xla_aot_compile_test_gpu_target_config.prototxt");
  stream_executor::GpuTargetConfigProto target_config;
  TF_ASSERT_OK(tsl::ReadTextProto(tsl::Env::Default(), target_config_path,
                                  &target_config));
  EXPECT_THAT(CompileExecutable(std::move(module_), "gpu", std::nullopt),
              IsOkAndHolds(Not(IsEmpty())));
}

TEST_F(XlaCompileLibTest, DISABLED_ON_GPU(ErrorsOnUnexpectedPlatform)) {
  EXPECT_THAT(CompileExecutable(nullptr, "tpu", std::nullopt),
              StatusIs(tsl::error::UNIMPLEMENTED));
}

TEST_F(XlaCompileLibTest, DISABLED_ON_GPU(WriteResultFilePropagatesErrors)) {
  TimerStats stats;
  CompilationResult result;
  EXPECT_THAT(WriteResultFile("/does/not/exist", stats, result), Not(IsOk()));
}

TEST_F(XlaCompileLibTest, DISABLED_ON_GPU(WriteResultFileWritesTheFile)) {
  std::string result_output_file;
  ASSERT_TRUE(tsl::Env::Default()->LocalTempFilename(&result_output_file));

  TimerStats stats;
  {
    absl::MutexLock ml(&stats.stats_mutex);
    stats.cumulative_secs = 5.5;
    stats.max_secs = 5.5;
  }

  CompilationResult result;
  google::protobuf::Duration duration;
  duration.set_seconds(5);
  duration.set_nanos(0.5 * tsl::EnvTime::kSecondsToNanos);
  *result.mutable_perf_stats()->mutable_compilation_duration() = duration;
  *result.mutable_perf_stats()->mutable_total_duration() = duration;

  TF_ASSERT_OK(WriteResultFile(result_output_file, stats, result));

  CompilationResult got_result;
  TF_ASSERT_OK(tsl::ReadBinaryProto(tsl::Env::Default(), result_output_file,
                                    &got_result));
  // Sadly EqualsProto isn't OSS, so we inspect a few fields manually.
  // See googletest#1761 and b/229726259.
  EXPECT_EQ(5, got_result.perf_stats().compilation_duration().seconds());
  EXPECT_EQ(0.5 * tsl::EnvTime::kSecondsToNanos,
            got_result.perf_stats().compilation_duration().nanos());
  EXPECT_EQ(5, got_result.perf_stats().total_duration().seconds());
  EXPECT_EQ(0.5 * tsl::EnvTime::kSecondsToNanos,
            got_result.perf_stats().total_duration().nanos());
}

}  // namespace
}  // namespace xla
