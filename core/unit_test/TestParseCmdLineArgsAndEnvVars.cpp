/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <gtest/gtest.h>

#include <impl/Kokkos_ParseCommandLineArgumentsAndEnvironmentVariables.hpp>
#include <impl/Kokkos_InitializationSettings.hpp>
#include <impl/Kokkos_DeviceManagement.hpp>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>

namespace {

class EnvVarsHelper {
  // do not let GTest run unit tests that set the environment concurrently
  static std::mutex mutex_;
  std::vector<std::string> vars_;
  // FIXME_CXX17 prefer optional
  // store name of env var that was already set (if any)
  // in which case unit test is skipped
  std::unique_ptr<std::string> skip_;

  void setup(std::unordered_map<std::string, std::string> const& vars) {
    for (auto const& x : vars) {
      auto const& name  = x.first;
      auto const& value = x.second;
      // skip unit test if env var is already set
      if (getenv(name.c_str())) {
        skip_ = std::make_unique<std::string>(name);
        break;
      }
#ifdef _WIN32
      int const error_code = _putenv((name + "=" + value).c_str());
#else
      int const error_code =
          setenv(name.c_str(), value.c_str(), /*overwrite=*/0);
#endif
      if (error_code != 0) {
        std::cerr << "failed to set environment variable '" << name << "="
                  << value << "'\n";
        std::abort();
      }
      vars_.push_back(name);
    }
  }
  void teardown() {
    for (auto const& name : vars_) {
#ifdef _WIN32
      int const error_code = _putenv((name + "=").c_str());
#else
      int const error_code = unsetenv(name.c_str());
#endif
      if (error_code != 0) {
        std::cerr << "failed to unset environment variable '" << name << "'\n";
        std::abort();
      }
    }
  }

 public:
  auto& skip() { return skip_; }
  EnvVarsHelper(std::unordered_map<std::string, std::string> const& vars) {
    mutex_.lock();
    setup(vars);
  }
  EnvVarsHelper& operator=(
      std::unordered_map<std::string, std::string> const& vars) {
    teardown();
    setup(vars);
    return *this;
  }
  ~EnvVarsHelper() {
    teardown();
    mutex_.unlock();
  }
  EnvVarsHelper(EnvVarsHelper&) = delete;
  EnvVarsHelper& operator=(EnvVarsHelper&) = delete;
  friend std::ostream& operator<<(std::ostream& os, EnvVarsHelper const& ev) {
    for (auto const& name : ev.vars_) {
      os << name << '=' << std::getenv(name.c_str()) << '\n';
    }
    return os;
  }
};
std::mutex EnvVarsHelper::mutex_;
#define SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev)       \
  if (ev.skip()) {                                         \
    GTEST_SKIP() << "environment variable '" << *ev.skip() \
                 << "' is already set";                    \
  }                                                        \
  static_assert(true, "no-op to require trailing semicolon")

class CmdLineArgsHelper {
  int argc_;
  std::vector<char*> argv_;
  std::vector<std::unique_ptr<char[]>> args_;

 public:
  CmdLineArgsHelper(std::vector<std::string> const& args) : argc_(args.size()) {
    for (auto const& x : args) {
      args_.emplace_back(new char[x.size() + 1]);
      char* ptr = args_.back().get();
      strcpy(ptr, x.c_str());
      argv_.push_back(ptr);
    }
  }
  int& argc() { return argc_; }
  char** argv() { return argv_.data(); }
};

TEST(defaultdevicetype, cmd_line_args_num_threads) {
  CmdLineArgsHelper cla = {{
      "--foo=bar",
      "--kokkos-num-threads=1",
      "--kokkos-num-threads=2",
  }};
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  EXPECT_TRUE(settings.has_num_threads());
  EXPECT_EQ(settings.get_num_threads(), 2);
  EXPECT_EQ(cla.argc(), 1);
  EXPECT_STREQ(*cla.argv(), "--foo=bar");
}

TEST(defaultdevicetype, cmd_line_args_device_id) {
  CmdLineArgsHelper cla = {{
      "--kokkos-device-id=3",
      "--dummy",
      "--kokkos-device-id=4",
  }};
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  EXPECT_TRUE(settings.has_device_id());
  EXPECT_EQ(settings.get_device_id(), 4);
  EXPECT_EQ(cla.argc(), 1);
  EXPECT_STREQ(*cla.argv(), "--dummy");
}

TEST(defaultdevicetype, cmd_line_args_num_devices) {
  CmdLineArgsHelper cla = {{
      "--kokkos-num-devices=5,6",
      "--kokkos-num-devices=7",
      "-v",
  }};
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  EXPECT_TRUE(settings.has_num_devices());
  EXPECT_EQ(settings.get_num_devices(), 7);
  // this is the current behavior, not suggesting this cannot be revisited
  EXPECT_TRUE(settings.has_skip_device()) << "behavior changed see comment";
  EXPECT_EQ(settings.get_skip_device(), 6) << "behavior changed see comment";
  EXPECT_EQ(cla.argc(), 1);
  EXPECT_STREQ(*cla.argv(), "-v");
}

TEST(defaultdevicetype, cmd_line_args_disable_warning) {
  CmdLineArgsHelper cla = {{
      "--kokkos-disable-warnings=1",
      "--kokkos-disable-warnings=false",
  }};
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  EXPECT_TRUE(settings.has_disable_warnings());
  EXPECT_FALSE(settings.get_disable_warnings());
}

TEST(defaultdevicetype, cmd_line_args_tune_internals) {
  CmdLineArgsHelper cla = {{
      "--kokkos-tune-internals",
      "--kokkos-num-threads=3",
  }};
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  EXPECT_TRUE(settings.has_tune_internals());
  EXPECT_TRUE(settings.get_tune_internals());
  EXPECT_TRUE(settings.has_num_threads());
  EXPECT_EQ(settings.get_num_threads(), 3);
}

TEST(defaultdevicetype, cmd_line_args_help) {
  CmdLineArgsHelper cla = {{
      "--help",
  }};
  Kokkos::InitializationSettings settings;
  ::testing::internal::CaptureStdout();
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  auto captured = ::testing::internal::GetCapturedStdout();
  // check that error message was only printed once
  EXPECT_EQ(captured.find("--kokkos-help"), captured.rfind("--kokkos-help"));
  EXPECT_EQ(cla.argc(), 1);
  EXPECT_STREQ(*cla.argv(), "--help");
  auto const help_message_length = captured.length();

  cla = {{
      {"--kokkos-help"},
  }};
  ::testing::internal::CaptureStdout();
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  captured = ::testing::internal::GetCapturedStdout();
  EXPECT_EQ(captured.length(), help_message_length);
  EXPECT_EQ(cla.argc(), 0);

  cla = {{
      {"--kokkos-help"},
      {"--help"},
      {"--kokkos-help"},
  }};
  ::testing::internal::CaptureStdout();
  Kokkos::Impl::parse_command_line_arguments(cla.argc(), cla.argv(), settings);
  captured = ::testing::internal::GetCapturedStdout();
  EXPECT_EQ(captured.length(), help_message_length);
  EXPECT_EQ(cla.argc(), 1);
  EXPECT_STREQ(*cla.argv(), "--help");
}

TEST(defaultdevicetype, env_vars_num_threads) {
  EnvVarsHelper ev = {{
      {"KOKKOS_NUM_THREADS", "24"},
      {"KOKKOS_DISABLE_WARNINGS", "1"},
  }};
  SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_environment_variables(settings);
  EXPECT_TRUE(settings.has_num_threads());
  EXPECT_EQ(settings.get_num_threads(), 24);
  EXPECT_TRUE(settings.has_disable_warnings());
  EXPECT_TRUE(settings.get_disable_warnings());

  ev = {{
      {"KOKKOS_NUM_THREADS", "1ABC"},
  }};
  SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
  settings = {};
  Kokkos::Impl::parse_environment_variables(settings);
  EXPECT_TRUE(settings.has_num_threads());
  EXPECT_EQ(settings.get_num_threads(), 1);
}

TEST(defaultdevicetype, env_vars_device_id) {
  EnvVarsHelper ev = {{
      {"KOKKOS_DEVICE_ID", "33"},
  }};
  SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_environment_variables(settings);
  EXPECT_TRUE(settings.has_device_id());
  EXPECT_EQ(settings.get_device_id(), 33);
}

TEST(defaultdevicetype, env_vars_num_devices) {
  EnvVarsHelper ev = {{
      {"KOKKOS_NUM_DEVICES", "4"},
      {"KOKKOS_SKIP_DEVICE", "1"},
  }};
  SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
  Kokkos::InitializationSettings settings;
  Kokkos::Impl::parse_environment_variables(settings);
  EXPECT_TRUE(settings.has_num_devices());
  EXPECT_EQ(settings.get_num_devices(), 4);
  EXPECT_TRUE(settings.has_skip_device());
  EXPECT_EQ(settings.get_skip_device(), 1);
}

TEST(defaultdevicetype, env_vars_disable_warnings) {
  for (auto const& value_true : {"1", "true", "TRUE", "yEs"}) {
    EnvVarsHelper ev = {{
        {"KOKKOS_DISABLE_WARNINGS", value_true},
    }};
    SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
    Kokkos::InitializationSettings settings;
    Kokkos::Impl::parse_environment_variables(settings);
    EXPECT_TRUE(settings.has_disable_warnings())
        << "KOKKOS_DISABLE_WARNINGS=" << value_true;
    EXPECT_TRUE(settings.get_disable_warnings())
        << "KOKKOS_DISABLE_WARNINGS=" << value_true;
  }
  for (auto const& value_false : {"0", "fAlse", "No"}) {
    EnvVarsHelper ev = {{
        {"KOKKOS_DISABLE_WARNINGS", value_false},
    }};
    SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
    Kokkos::InitializationSettings settings;
    Kokkos::Impl::parse_environment_variables(settings);
    EXPECT_TRUE(settings.has_disable_warnings())
        << "KOKKOS_DISABLE_WARNINGS=" << value_false;
    EXPECT_FALSE(settings.get_disable_warnings())
        << "KOKKOS_DISABLE_WARNINGS=" << value_false;
  }
}

TEST(defaultdevicetype, env_vars_tune_internals) {
  for (auto const& value_true : {"1", "yES", "true", "TRUE", "tRuE"}) {
    EnvVarsHelper ev = {{
        {"KOKKOS_TUNE_INTERNALS", value_true},
    }};
    SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
    Kokkos::InitializationSettings settings;
    Kokkos::Impl::parse_environment_variables(settings);
    EXPECT_TRUE(settings.has_tune_internals())
        << "KOKKOS_TUNE_INTERNALS=" << value_true;
    EXPECT_TRUE(settings.get_tune_internals())
        << "KOKKOS_TUNE_INTERNALS=" << value_true;
  }
  for (auto const& value_false : {"0", "false", "no"}) {
    EnvVarsHelper ev = {{
        {"KOKKOS_TUNE_INTERNALS", value_false},
    }};
    SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);
    Kokkos::InitializationSettings settings;
    Kokkos::Impl::parse_environment_variables(settings);
    EXPECT_TRUE(settings.has_tune_internals())
        << "KOKKOS_TUNE_INTERNALS=" << value_false;
    EXPECT_FALSE(settings.get_tune_internals())
        << "KOKKOS_TUNE_INTERNALS=" << value_false;
  }
}

TEST(defaultdevicetype, visible_devices) {
#define KOKKOS_TEST_VISIBLE_DEVICES(ENV, CNT, DEV)                    \
  do {                                                                \
    EnvVarsHelper ev{ENV};                                            \
    SKIP_IF_ENVIRONMENT_VARIABLE_ALREADY_SET(ev);                     \
    Kokkos::InitializationSettings settings;                          \
    Kokkos::Impl::parse_environment_variables(settings);              \
    auto computed = Kokkos::Impl::get_visible_devices(settings, CNT); \
    std::vector<int> expected = DEV;                                  \
    EXPECT_EQ(expected.size(), computed.size())                       \
        << ev << "device count: " << CNT;                             \
    auto n = std::min<int>(expected.size(), computed.size());         \
    for (int i = 0; i < n; ++i) {                                     \
      EXPECT_EQ(expected[i], computed[i])                             \
          << "devices differ at index " << i << '\n'                  \
          << ev << "device count: " << CNT;                           \
    }                                                                 \
  } while (false)

#define DEV(...) \
  std::vector<int> { __VA_ARGS__ }
#define ENV(...) std::unordered_map<std::string, std::string>{__VA_ARGS__}

  // first test with all environment variables that are involved in determining
  // the visible devices so user set var do not mess up the logic below.
  KOKKOS_TEST_VISIBLE_DEVICES(
      ENV({"KOKKOS_VISIBLE_DEVICES", "2,1"}, {"KOKKOS_NUM_DEVICES", "8"},
          {"KOKKOS_SKIP_DEVICE", "1"}),
      6, DEV(2, 1));
  KOKKOS_TEST_VISIBLE_DEVICES(
      ENV({"KOKKOS_VISIBLE_DEVICES", "2,1"}, {"KOKKOS_NUM_DEVICES", "8"}, ), 6,
      DEV(2, 1));
  KOKKOS_TEST_VISIBLE_DEVICES(ENV({"KOKKOS_NUM_DEVICES", "3"}), 6,
                              DEV(0, 1, 2));
  KOKKOS_TEST_VISIBLE_DEVICES(
      ENV({"KOKKOS_NUM_DEVICES", "4"}, {"KOKKOS_SKIP_DEVICE", "1"}, ), 6,
      DEV(0, 2, 3));
  KOKKOS_TEST_VISIBLE_DEVICES(ENV({"KOKKOS_VISIBLE_DEVICES", "1,3,4"}), 6,
                              DEV(1, 3, 4));
  KOKKOS_TEST_VISIBLE_DEVICES(
      ENV({"KOKKOS_VISIBLE_DEVICES", "2,1"}, {"KOKKOS_SKIP_DEVICE", "1"}, ), 6,
      DEV(2, 1));
  KOKKOS_TEST_VISIBLE_DEVICES(ENV(), 4, DEV(0, 1, 2, 3));

#undef ENV
#undef DEV
#undef KOKKOS_TEST_VISIBLE_DEVICES
}

}  // namespace