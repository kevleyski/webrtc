/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "api/test/create_network_emulation_manager.h"
#include "api/test/create_peerconnection_quality_test_fixture.h"
#include "api/test/network_emulation_manager.h"
#include "api/test/peerconnection_quality_test_fixture.h"
#include "api/test/simulated_network.h"
#include "call/simulated_network.h"
#include "media/base/vp9_profile.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "system_wrappers/include/field_trial.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/pc/e2e/network_quality_metrics_reporter.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

using PeerConfigurer =
    webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::PeerConfigurer;
using RunParams = webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::RunParams;
using VideoConfig =
    webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::VideoConfig;
using AudioConfig =
    webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::AudioConfig;
using VideoGeneratorType =
    webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::VideoGeneratorType;

namespace {

constexpr int kTestDurationSec = 45;
constexpr char kVp8TrustedRateControllerFieldTrial[] =
    "WebRTC-LibvpxVp8TrustedRateController/Enabled/";

EmulatedNetworkNode* CreateEmulatedNodeWithConfig(
    NetworkEmulationManager* emulation,
    const BuiltInNetworkBehaviorConfig& config) {
  return emulation->CreateEmulatedNode(
      absl::make_unique<SimulatedNetwork>(config));
}

std::pair<EmulatedNetworkManagerInterface*, EmulatedNetworkManagerInterface*>
CreateTwoNetworkLinks(NetworkEmulationManager* emulation,
                      const BuiltInNetworkBehaviorConfig& config) {
  auto* alice_node = CreateEmulatedNodeWithConfig(emulation, config);
  auto* bob_node = CreateEmulatedNodeWithConfig(emulation, config);

  auto* alice_endpoint = emulation->CreateEndpoint(EmulatedEndpointConfig());
  auto* bob_endpoint = emulation->CreateEndpoint(EmulatedEndpointConfig());

  emulation->CreateRoute(alice_endpoint, {alice_node}, bob_endpoint);
  emulation->CreateRoute(bob_endpoint, {bob_node}, alice_endpoint);

  return {
      emulation->CreateEmulatedNetworkManagerInterface({alice_endpoint}),
      emulation->CreateEmulatedNetworkManagerInterface({bob_endpoint}),
  };
}

std::unique_ptr<webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture>
CreateTestFixture(const std::string& test_case_name,
                  std::pair<EmulatedNetworkManagerInterface*,
                            EmulatedNetworkManagerInterface*> network_links,
                  rtc::FunctionView<void(PeerConfigurer*)> alice_configurer,
                  rtc::FunctionView<void(PeerConfigurer*)> bob_configurer) {
  auto fixture = webrtc_pc_e2e::CreatePeerConnectionE2EQualityTestFixture(
      test_case_name, /*audio_quality_analyzer=*/nullptr,
      /*video_quality_analyzer=*/nullptr);
  fixture->AddPeer(network_links.first->network_thread(),
                   network_links.first->network_manager(), alice_configurer);
  fixture->AddPeer(network_links.second->network_thread(),
                   network_links.second->network_manager(), bob_configurer);
  fixture->AddQualityMetricsReporter(
      absl::make_unique<webrtc_pc_e2e::NetworkQualityMetricsReporter>(
          network_links.first, network_links.second));
  return fixture;
}

// Takes the current active field trials set, and appends some new trials.
std::string AppendFieldTrials(std::string new_trial_string) {
  return std::string(field_trial::GetFieldTrialString()) + new_trial_string;
}

std::string ClipNameToClipPath(const char* clip_name) {
  return test::ResourcePath(clip_name, "yuv");
}

}  // namespace

class PCGenericDescriptorTest : public ::testing::TestWithParam<std::string> {
 public:
  PCGenericDescriptorTest()
      : field_trial_(AppendFieldTrials(GetParam())),
        generic_descriptor_enabled_(
            field_trial::IsEnabled("WebRTC-GenericDescriptor")) {}

  std::string GetTestName(std::string base) {
    if (generic_descriptor_enabled_)
      base += "_generic_descriptor";
    return base;
  }

 private:
  test::ScopedFieldTrials field_trial_;
  bool generic_descriptor_enabled_;
};

#if defined(RTC_ENABLE_VP9)
TEST(PCFullStackTest, ForemanCifWithoutPacketLossVp9) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_net_delay_0_0_plr_0_VP9",
      CreateTwoNetworkLinks(network_emulation_manager.get(),
                            BuiltInNetworkBehaviorConfig()),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp9CodecName;
  run_params.video_codec_required_params = {
      {kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile0)}};
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCifPlr5Vp9) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_delay_50_0_plr_5_VP9"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp9CodecName;
  run_params.video_codec_required_params = {
      {kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile0)}};
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, GeneratorWithoutPacketLossVp9Profile2) {
  bool profile_2_is_supported = false;
  for (const auto& codec : SupportedVP9Codecs()) {
    if (ParseSdpForVP9Profile(codec.parameters)
            .value_or(VP9Profile::kProfile0) == VP9Profile::kProfile2) {
      profile_2_is_supported = true;
    }
  }
  if (!profile_2_is_supported)
    return;
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  auto fixture = CreateTestFixture(
      "pc_generator_net_delay_0_0_plr_0_VP9Profile2",
      CreateTwoNetworkLinks(network_emulation_manager.get(),
                            BuiltInNetworkBehaviorConfig()),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.generator = VideoGeneratorType::kI010;
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp9CodecName;
  run_params.video_codec_required_params = {
      {kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile2)}};
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

/*
// TODO(bugs.webrtc.org/10639) migrate commented out test, when required
// functionality will be supported in PeerConnection level framework.
TEST(PCFullStackTest, ForemanCifWithoutPacketLossMultiplexI420Frame) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging foreman_cif;
  foreman_cif.call.send_side_bwe = true;
  foreman_cif.video[0] = {
      true,        352,    288,    30,
      700000,      700000, 700000, false,
      "multiplex", 1,      0,      0,
      false,       false,  false,  ClipNameToClipPath("foreman_cif")};
  foreman_cif.analyzer = {"foreman_cif_net_delay_0_0_plr_0_Multiplex", 0.0, 0.0,
                          kTestDurationSec};
  fixture->RunWithAnalyzer(foreman_cif);
}

TEST(PCFullStackTest, GeneratorWithoutPacketLossMultiplexI420AFrame) {
  auto fixture = CreateVideoQualityTestFixture();

  ParamsWithLogging generator;
  generator.call.send_side_bwe = true;
  generator.video[0] = {
      true,        352, 288, 30, 700000, 700000, 700000, false,
      "multiplex", 1,   0,   0,  false,  false,  false,  "GeneratorI420A"};
  generator.analyzer = {"generator_net_delay_0_0_plr_0_Multiplex", 0.0, 0.0,
                        kTestDurationSec};
  fixture->RunWithAnalyzer(generator);
}
*/
#endif  // defined(RTC_ENABLE_VP9)

TEST(PCFullStackTest, ParisQcifWithoutPacketLoss) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  auto fixture = CreateTestFixture(
      "pc_net_delay_0_0_plr_0",
      CreateTwoNetworkLinks(network_emulation_manager.get(),
                            BuiltInNetworkBehaviorConfig()),
      [](PeerConfigurer* alice) {
        VideoConfig video(176, 144, 30);
        video.input_file_name = ClipNameToClipPath("paris_qcif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCifWithoutPacketLoss) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_net_delay_0_0_plr_0"),
      CreateTwoNetworkLinks(network_emulation_manager.get(),
                            BuiltInNetworkBehaviorConfig()),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCif35kbpsWithoutPacketLoss) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 35;
  auto fixture = CreateTestFixture(
      GetTestName("foreman_cif_30kbps_net_delay_0_0_plr_0"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 10);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// TODO(webrtc:9722): Remove when experiment is cleaned up.
TEST_P(PCGenericDescriptorTest,
       ForemanCif35kbpsWithoutPacketLossTrustedRateControl) {
  test::ScopedFieldTrials override_field_trials(
      AppendFieldTrials(kVp8TrustedRateControllerFieldTrial));
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 35;
  auto fixture = CreateTestFixture(
      GetTestName("foreman_cif_30kbps_net_delay_0_0_plr_0_trusted_rate_ctrl"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 10);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// Link capacity below default start rate.
TEST(PCFullStackTest, ForemanCifLink150kbpsWithoutPacketLoss) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 150;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_link_150kbps_net_delay_0_0_plr_0",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// Restricted network and encoder overproducing by 30%.
TEST(PCFullStackTest, ForemanCifLink150kbpsBadRateController) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 150;
  config.queue_length_packets = 30;
  config.queue_delay_ms = 100;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_link_150kbps_delay100ms_30pkts_queue_overshoot30",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  run_params.video_encoder_bitrate_multiplier = 1.30;
  fixture->Run(std::move(run_params));
}

// Weak 3G-style link: 250kbps, 1% loss, 100ms delay, 15 packets queue.
// Packet rate and loss are low enough that loss will happen with ~3s interval.
// This triggers protection overhead to toggle between zero and non-zero.
// Link queue is restrictive enough to trigger loss on probes.
TEST(PCFullStackTest, ForemanCifMediaCapacitySmallLossAndQueue) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 250;
  config.queue_length_packets = 10;
  config.queue_delay_ms = 100;
  config.loss_percent = 1;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_link_250kbps_delay100ms_10pkts_loss1",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  run_params.video_encoder_bitrate_multiplier = 1.30;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCifPlr5) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_delay_50_0_plr_5"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCifPlr5Ulpfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_delay_50_0_plr_5_ulpfec"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = true;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCifPlr5Flexfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_delay_50_0_plr_5_flexfec",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = true;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCif500kbpsPlr3Flexfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 3;
  config.link_capacity_kbps = 500;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_500kbps_delay_50_0_plr_3_flexfec",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = true;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCif500kbpsPlr3Ulpfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 3;
  config.link_capacity_kbps = 500;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_500kbps_delay_50_0_plr_3_ulpfec",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = true;
  fixture->Run(std::move(run_params));
}

#if defined(WEBRTC_USE_H264)
TEST(PCFullStackTest, ForemanCifWithoutPacketlossH264) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_net_delay_0_0_plr_0_H264",
      CreateTwoNetworkLinks(network_emulation_manager.get(),
                            BuiltInNetworkBehaviorConfig()),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCif35kbpsWithoutPacketlossH264) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.link_capacity_kbps = 35;
  auto fixture = CreateTestFixture(
      "foreman_cif_30kbps_net_delay_0_0_plr_0_H264",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 10);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCifPlr5H264) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_delay_50_0_plr_5_H264"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCifPlr5H264SpsPpsIdrIsKeyframe) {
  test::ScopedFieldTrials override_field_trials(
      AppendFieldTrials("WebRTC-SpsPpsIdrIsH264Keyframe/Enabled/"));

  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_delay_50_0_plr_5_H264_sps_pps_idr",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCifPlr5H264Flexfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_delay_50_0_plr_5_H264_flexfec",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = true;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// Ulpfec with H264 is an unsupported combination, so this test is only useful
// for debugging. It is therefore disabled by default.
TEST(PCFullStackTest, DISABLED_ForemanCifPlr5H264Ulpfec) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.loss_percent = 5;
  config.queue_delay_ms = 50;
  auto fixture = CreateTestFixture(
      "foreman_cif_delay_50_0_plr_5_H264_ulpfec",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kH264CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = true;
  fixture->Run(std::move(run_params));
}
#endif  // defined(WEBRTC_USE_H264)

TEST(PCFullStackTest, ForemanCif500kbps) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 0;
  config.queue_delay_ms = 0;
  config.link_capacity_kbps = 500;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_500kbps",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCif500kbpsLimitedQueue) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 32;
  config.queue_delay_ms = 0;
  config.link_capacity_kbps = 500;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_500kbps_32pkts_queue",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST(PCFullStackTest, ForemanCif500kbps100ms) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 0;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 500;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_500kbps_100ms",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

TEST_P(PCGenericDescriptorTest, ForemanCif500kbps100msLimitedQueue) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 32;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 500;
  auto fixture = CreateTestFixture(
      GetTestName("pc_foreman_cif_500kbps_100ms_32pkts_queue"),
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

/*
// TODO(bugs.webrtc.org/10639) we need to disable send side bwe, but it isn't
supported in
// PC level framework.
TEST(PCFullStackTest, ForemanCif500kbps100msLimitedQueueRecvBwe) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging foreman_cif;
  foreman_cif.call.send_side_bwe = false;
  foreman_cif.video[0] = {
      true,  352,    288,     30,
      30000, 500000, 2000000, false,
      "VP8", 1,      0,       0,
      false, false,  true,    ClipNameToClipPath("foreman_cif")};
  foreman_cif.analyzer = {"foreman_cif_500kbps_100ms_32pkts_queue_recv_bwe",
                          0.0, 0.0, kTestDurationSec};
  foreman_cif.config->queue_length_packets = 32;
  foreman_cif.config->queue_delay_ms = 100;
  foreman_cif.config->link_capacity_kbps = 500;
  fixture->RunWithAnalyzer(foreman_cif);
}
*/

TEST(PCFullStackTest, ForemanCif1000kbps100msLimitedQueue) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 32;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 1000;
  auto fixture = CreateTestFixture(
      "pc_foreman_cif_1000kbps_100ms_32pkts_queue",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(352, 288, 30);
        video.input_file_name = ClipNameToClipPath("foreman_cif");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// TODO(sprang): Remove this if we have the similar ModerateLimits below?
TEST(PCFullStackTest, ConferenceMotionHd2000kbps100msLimitedQueue) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 32;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 2000;
  auto fixture = CreateTestFixture(
      "pc_conference_motion_hd_2000kbps_100ms_32pkts_queue",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(1280, 720, 50);
        video.input_file_name =
            ClipNameToClipPath("ConferenceMotion_1280_720_50");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

// TODO(webrtc:9722): Remove when experiment is cleaned up.
TEST(PCFullStackTest, ConferenceMotionHd1TLModerateLimitsWhitelistVp8) {
  test::ScopedFieldTrials override_field_trials(
      AppendFieldTrials(kVp8TrustedRateControllerFieldTrial));
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 50;
  config.loss_percent = 3;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 2000;
  auto fixture = CreateTestFixture(
      "pc_conference_motion_hd_1tl_moderate_limits_trusted_rate_ctrl",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(1280, 720, 50);
        video.input_file_name =
            ClipNameToClipPath("ConferenceMotion_1280_720_50");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp8CodecName;
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}

/*
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST_P(PCGenericDescriptorTest, ConferenceMotionHd2TLModerateLimits) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging conf_motion_hd;
  conf_motion_hd.call.send_side_bwe = true;
  conf_motion_hd.video[0] = {
      true,    1280,
      720,     50,
      30000,   3000000,
      3000000, false,
      "VP8",   2,
      -1,      0,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
  conf_motion_hd.analyzer = {
      GetTestName("conference_motion_hd_2tl_moderate_limits"), 0.0, 0.0,
      kTestDurationSec};
  conf_motion_hd.config->queue_length_packets = 50;
  conf_motion_hd.config->loss_percent = 3;
  conf_motion_hd.config->queue_delay_ms = 100;
  conf_motion_hd.config->link_capacity_kbps = 2000;
  conf_motion_hd.call.generic_descriptor = GenericDescriptorEnabled();
  fixture->RunWithAnalyzer(conf_motion_hd);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ConferenceMotionHd3TLModerateLimits) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging conf_motion_hd;
  conf_motion_hd.call.send_side_bwe = true;
  conf_motion_hd.video[0] = {
      true,    1280,
      720,     50,
      30000,   3000000,
      3000000, false,
      "VP8",   3,
      -1,      0,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
  conf_motion_hd.analyzer = {"conference_motion_hd_3tl_moderate_limits", 0.0,
                             0.0, kTestDurationSec};
  conf_motion_hd.config->queue_length_packets = 50;
  conf_motion_hd.config->loss_percent = 3;
  conf_motion_hd.config->queue_delay_ms = 100;
  conf_motion_hd.config->link_capacity_kbps = 2000;
  fixture->RunWithAnalyzer(conf_motion_hd);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ConferenceMotionHd4TLModerateLimits) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging conf_motion_hd;
  conf_motion_hd.call.send_side_bwe = true;
  conf_motion_hd.video[0] = {
      true,    1280,
      720,     50,
      30000,   3000000,
      3000000, false,
      "VP8",   4,
      -1,      0,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
  conf_motion_hd.analyzer = {"conference_motion_hd_4tl_moderate_limits", 0.0,
                             0.0, kTestDurationSec};
  conf_motion_hd.config->queue_length_packets = 50;
  conf_motion_hd.config->loss_percent = 3;
  conf_motion_hd.config->queue_delay_ms = 100;
  conf_motion_hd.config->link_capacity_kbps = 2000;
  fixture->RunWithAnalyzer(conf_motion_hd);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ConferenceMotionHd3TLModerateLimitsAltTLPattern) {
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials("WebRTC-UseShortVP8TL3Pattern/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging conf_motion_hd;
  conf_motion_hd.call.send_side_bwe = true;
  conf_motion_hd.video[0] = {
      true,    1280,
      720,     50,
      30000,   3000000,
      3000000, false,
      "VP8",   3,
      -1,      0,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
  conf_motion_hd.analyzer = {"conference_motion_hd_3tl_alt_moderate_limits",
                             0.0, 0.0, kTestDurationSec};
  conf_motion_hd.config->queue_length_packets = 50;
  conf_motion_hd.config->loss_percent = 3;
  conf_motion_hd.config->queue_delay_ms = 100;
  conf_motion_hd.config->link_capacity_kbps = 2000;
  fixture->RunWithAnalyzer(conf_motion_hd);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest,
     ConferenceMotionHd3TLModerateLimitsAltTLPatternAndBaseHeavyTLAllocation) {
  auto fixture = CreateVideoQualityTestFixture();
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials("WebRTC-UseShortVP8TL3Pattern/Enabled/"
                        "WebRTC-UseBaseHeavyVP8TL3RateAllocation/Enabled/"));
  ParamsWithLogging conf_motion_hd;
  conf_motion_hd.call.send_side_bwe = true;
  conf_motion_hd.video[0] = {
      true,    1280,
      720,     50,
      30000,   3000000,
      3000000, false,
      "VP8",   3,
      -1,      0,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
  conf_motion_hd.analyzer = {
      "conference_motion_hd_3tl_alt_heavy_moderate_limits", 0.0, 0.0,
      kTestDurationSec};
  conf_motion_hd.config->queue_length_packets = 50;
  conf_motion_hd.config->loss_percent = 3;
  conf_motion_hd.config->queue_delay_ms = 100;
  conf_motion_hd.config->link_capacity_kbps = 2000;
  fixture->RunWithAnalyzer(conf_motion_hd);
}
*/

#if defined(RTC_ENABLE_VP9)
TEST(PCFullStackTest, ConferenceMotionHd2000kbps100msLimitedQueueVP9) {
  std::unique_ptr<NetworkEmulationManager> network_emulation_manager =
      CreateNetworkEmulationManager();
  BuiltInNetworkBehaviorConfig config;
  config.queue_length_packets = 32;
  config.queue_delay_ms = 100;
  config.link_capacity_kbps = 2000;
  auto fixture = CreateTestFixture(
      "pc_conference_motion_hd_2000kbps_100ms_32pkts_queue_vp9",
      CreateTwoNetworkLinks(network_emulation_manager.get(), config),
      [](PeerConfigurer* alice) {
        VideoConfig video(1280, 720, 50);
        video.input_file_name =
            ClipNameToClipPath("ConferenceMotion_1280_720_50");
        video.stream_label = "alice-video";
        alice->AddVideoConfig(std::move(video));
      },
      [](PeerConfigurer* bob) {});
  RunParams run_params(TimeDelta::seconds(kTestDurationSec));
  run_params.video_codec_name = cricket::kVp9CodecName;
  run_params.video_codec_required_params = {
      {kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile0)}};
  run_params.use_flex_fec = false;
  run_params.use_ulp_fec = false;
  fixture->Run(std::move(run_params));
}
#endif

/*
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  5, 50000, 200000,
                          1000000, false, "VP8", 2, 1,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides", 0.0, 0.0, kTestDurationSec};
  fixture->RunWithAnalyzer(screenshare);
}

#if !defined(WEBRTC_MAC)
// All the tests using this constant are disabled on Mac.
const char kScreenshareSimulcastExperiment[] =
    "WebRTC-SimulcastScreenshare/Enabled/";
// TODO(bugs.webrtc.org/9840): Investigate why is this test flaky on Win/Mac.
#if !defined(WEBRTC_WIN)
const char kScreenshareSimulcastVariableFramerateExperiment[] =
    "WebRTC-SimulcastScreenshare/Enabled/"
    "WebRTC-VP8VariableFramerateScreenshare/"
    "Enabled,min_fps:5.0,min_qp:15,undershoot:30/";
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_Simulcast) {
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials(kScreenshareSimulcastExperiment));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.video[0] = {true,    1850,  1110,  30, 800000, 2500000,
                          2500000, false, "VP8", 2,  1,      400000,
                          false,   false, false, ""};
  screenshare.analyzer = {"screenshare_slides_simulcast", 0.0, 0.0,
                          kTestDurationSec};
  ParamsWithLogging screenshare_params_high;
  screenshare_params_high.video[0] = {
      true,  1850, 1110, 60,     600000, 1250000, 1250000, false,
      "VP8", 2,    0,    400000, false,  false,   false,   ""};
  VideoQualityTest::Params screenshare_params_low;
  screenshare_params_low.video[0] = {true,    1850,  1110,  5, 30000, 200000,
                                     1000000, false, "VP8", 2, 0,     400000,
                                     false,   false, false, ""};

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(screenshare_params_low, 0),
      VideoQualityTest::DefaultVideoStream(screenshare_params_high, 0)};
  screenshare.ss[0] = {
      streams, 1, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_Simulcast_Variable_Framerate) {
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials(kScreenshareSimulcastVariableFramerateExperiment));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.video[0] = {true,    1850,  1110,  30, 800000, 2500000,
                          2500000, false, "VP8", 2,  1,      400000,
                          false,   false, false, ""};
  screenshare.analyzer = {"screenshare_slides_simulcast_variable_framerate",
                          0.0, 0.0, kTestDurationSec};
  ParamsWithLogging screenshare_params_high;
  screenshare_params_high.video[0] = {
      true,  1850, 1110, 60,     600000, 1250000, 1250000, false,
      "VP8", 2,    0,    400000, false,  false,   false,   ""};
  VideoQualityTest::Params screenshare_params_low;
  screenshare_params_low.video[0] = {true,    1850,  1110,  5, 30000, 200000,
                                     1000000, false, "VP8", 2, 0,     400000,
                                     false,   false, false, ""};

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(screenshare_params_low, 0),
      VideoQualityTest::DefaultVideoStream(screenshare_params_high, 0)};
  screenshare.ss[0] = {
      streams, 1, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_Simulcast_low) {
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials(kScreenshareSimulcastExperiment));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.video[0] = {true,    1850,  1110,  30, 800000, 2500000,
                          2500000, false, "VP8", 2,  1,      400000,
                          false,   false, false, ""};
  screenshare.analyzer = {"screenshare_slides_simulcast_low", 0.0, 0.0,
                          kTestDurationSec};
  VideoQualityTest::Params screenshare_params_high;
  screenshare_params_high.video[0] = {
      true,  1850, 1110, 60,     600000, 1250000, 1250000, false,
      "VP8", 2,    0,    400000, false,  false,   false,   ""};
  VideoQualityTest::Params screenshare_params_low;
  screenshare_params_low.video[0] = {true,    1850,  1110,  5, 30000, 200000,
                                     1000000, false, "VP8", 2, 0,     400000,
                                     false,   false, false, ""};

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(screenshare_params_low, 0),
      VideoQualityTest::DefaultVideoStream(screenshare_params_high, 0)};
  screenshare.ss[0] = {
      streams, 0, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(screenshare);
}

#endif  // !defined(WEBRTC_WIN)
#endif  // !defined(WEBRTC_MAC)

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_Scroll) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging config;
  config.call.send_side_bwe = true;
  config.video[0] = {true,    1850,  1110 / 2, 5, 50000, 200000,
                     1000000, false, "VP8",    2, 1,     400000,
                     false,   false, false,    ""};
  config.screenshare[0] = {true, false, 10, 2};
  config.analyzer = {"screenshare_slides_scrolling", 0.0, 0.0,
                     kTestDurationSec};
  fixture->RunWithAnalyzer(config);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST_P(PCGenericDescriptorTest, ScreenshareSlidesVP8_2TL_LossyNet) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  5, 50000, 200000,
                          1000000, false, "VP8", 2, 1,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {GetTestName("screenshare_slides_lossy_net"), 0.0, 0.0,
                          kTestDurationSec};
  screenshare.config->loss_percent = 5;
  screenshare.config->queue_delay_ms = 200;
  screenshare.config->link_capacity_kbps = 500;
  screenshare.call.generic_descriptor = GenericDescriptorEnabled();
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_VeryLossyNet) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  5, 50000, 200000,
                          1000000, false, "VP8", 2, 1,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides_very_lossy", 0.0, 0.0,
                          kTestDurationSec};
  screenshare.config->loss_percent = 10;
  screenshare.config->queue_delay_ms = 200;
  screenshare.config->link_capacity_kbps = 500;
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_LossyNetRestrictedQueue) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  5, 50000, 200000,
                          1000000, false, "VP8", 2, 1,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides_lossy_limited", 0.0, 0.0,
                          kTestDurationSec};
  screenshare.config->loss_percent = 5;
  screenshare.config->link_capacity_kbps = 200;
  screenshare.config->queue_length_packets = 30;

  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP8_2TL_ModeratelyRestricted) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  5, 50000, 200000,
                          1000000, false, "VP8", 2, 1,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides_moderately_restricted", 0.0, 0.0,
                          kTestDurationSec};
  screenshare.config->loss_percent = 1;
  screenshare.config->link_capacity_kbps = 1200;
  screenshare.config->queue_length_packets = 30;

  fixture->RunWithAnalyzer(screenshare);
}

namespace {
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
// Since ParamsWithLogging::Video is not trivially destructible, we can't
// store these structs as const globals.
ParamsWithLogging::Video SvcVp9Video() {
  return ParamsWithLogging::Video{
      true,    1280,
      720,     30,
      800000,  2500000,
      2500000, false,
      "VP9",   3,
      2,       400000,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
}

ParamsWithLogging::Video SimulcastVp8VideoHigh() {
  return ParamsWithLogging::Video{
      true,    1280,
      720,     30,
      800000,  2500000,
      2500000, false,
      "VP8",   3,
      2,       400000,
      false,   false,
      false,   ClipNameToClipPath("ConferenceMotion_1280_720_50")};
}

ParamsWithLogging::Video SimulcastVp8VideoMedium() {
  return ParamsWithLogging::Video{
      true,   640,
      360,    30,
      150000, 500000,
      700000, false,
      "VP8",  3,
      2,      400000,
      false,  false,
      false,  ClipNameToClipPath("ConferenceMotion_1280_720_50")};
}

ParamsWithLogging::Video SimulcastVp8VideoLow() {
  return ParamsWithLogging::Video{
      true,   320,
      180,    30,
      30000,  150000,
      200000, false,
      "VP8",  3,
      2,      400000,
      false,  false,
      false,  ClipNameToClipPath("ConferenceMotion_1280_720_50")};
}
}  // namespace

#if defined(RTC_ENABLE_VP9)

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP9_3SL_High_Fps) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  30, 50000, 200000,
                          2000000, false, "VP9", 1,  0,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides_vp9_3sl_high_fps", 0.0, 0.0,
                          kTestDurationSec};
  screenshare.ss[0] = {
      std::vector<VideoStream>(),  0,   3, 2, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), true};
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, ScreenshareSlidesVP9_3SL_Variable_Fps) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-VP9VariableFramerateScreenshare/"
                        "Enabled,min_qp:32,min_fps:5.0,undershoot:30,frames_"
                        "before_steady_state:5/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging screenshare;
  screenshare.call.send_side_bwe = true;
  screenshare.video[0] = {true,    1850,  1110,  30, 50000, 200000,
                          2000000, false, "VP9", 1,  0,     400000,
                          false,   false, false, ""};
  screenshare.screenshare[0] = {true, false, 10};
  screenshare.analyzer = {"screenshare_slides_vp9_3sl_variable_fps", 0.0, 0.0,
                          kTestDurationSec};
  screenshare.ss[0] = {
      std::vector<VideoStream>(),  0,   3, 2, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), true};
  fixture->RunWithAnalyzer(screenshare);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9SVC_3SL_High) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9svc_3sl_high", 0.0, 0.0, kTestDurationSec};

  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 2, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9SVC_3SL_Medium) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9svc_3sl_medium", 0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 1, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9SVC_3SL_Low) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9svc_3sl_low", 0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 0, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// bugs.webrtc.org/9506
#if !defined(WEBRTC_MAC)

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9KSVC_3SL_High) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-Vp9IssueKeyFrameOnLayerDeactivation/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9ksvc_3sl_high", 0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 2, InterLayerPredMode::kOnKeyPic,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9KSVC_3SL_Medium) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-Vp9IssueKeyFrameOnLayerDeactivation/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9ksvc_3sl_medium", 0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 1, InterLayerPredMode::kOnKeyPic,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9KSVC_3SL_Low) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-Vp9IssueKeyFrameOnLayerDeactivation/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9ksvc_3sl_low", 0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, 0, InterLayerPredMode::kOnKeyPic,
      std::vector<SpatialLayer>(), false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, VP9KSVC_3SL_Medium_Network_Restricted) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-Vp9IssueKeyFrameOnLayerDeactivation/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9ksvc_3sl_medium_network_restricted", 0.0, 0.0,
                        kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, -1, InterLayerPredMode::kOnKeyPic,
      std::vector<SpatialLayer>(), false};
  simulcast.config->link_capacity_kbps = 1000;
  simulcast.config->queue_delay_ms = 100;
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
// TODO(webrtc:9722): Remove when experiment is cleaned up.
TEST(PCFullStackTest, VP9KSVC_3SL_Medium_Network_Restricted_Trusted_Rate) {
  webrtc::test::ScopedFieldTrials override_trials(
      AppendFieldTrials("WebRTC-Vp9IssueKeyFrameOnLayerDeactivation/Enabled/"
                        "WebRTC-LibvpxVp9TrustedRateController/Enabled/"));
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SvcVp9Video();
  simulcast.analyzer = {"vp9ksvc_3sl_medium_network_restricted_trusted_rate",
                        0.0, 0.0, kTestDurationSec};
  simulcast.ss[0] = {
      std::vector<VideoStream>(),  0,    3, -1, InterLayerPredMode::kOnKeyPic,
      std::vector<SpatialLayer>(), false};
  simulcast.config->link_capacity_kbps = 1000;
  simulcast.config->queue_delay_ms = 100;
  fixture->RunWithAnalyzer(simulcast);
}
#endif  // !defined(WEBRTC_MAC)

#endif  // defined(RTC_ENABLE_VP9)

// Android bots can't handle FullHD, so disable the test.
// TODO(bugs.webrtc.org/9220): Investigate source of flakiness on Mac.
#if defined(WEBRTC_ANDROID) || defined(WEBRTC_MAC)
#define MAYBE_SimulcastFullHdOveruse DISABLED_SimulcastFullHdOveruse
#else
#define MAYBE_SimulcastFullHdOveruse SimulcastFullHdOveruse
#endif
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, MAYBE_SimulcastFullHdOveruse) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = {true,    1920,  1080,  30,         800000, 2500000,
                        2500000, false, "VP8", 3,          2,      400000,
                        false,   false, false, "Generator"};
  simulcast.analyzer = {"simulcast_HD_high", 0.0, 0.0, kTestDurationSec};
  simulcast.config->loss_percent = 0;
  simulcast.config->queue_delay_ms = 100;
  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(simulcast, 0),
      VideoQualityTest::DefaultVideoStream(simulcast, 0),
      VideoQualityTest::DefaultVideoStream(simulcast, 0)};
  simulcast.ss[0] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      true};
  webrtc::test::ScopedFieldTrials override_trials(AppendFieldTrials(
      "WebRTC-ForceSimulatedOveruseIntervalMs/1000-50000-300/"));
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, SimulcastVP8_3SL_High) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SimulcastVp8VideoHigh();
  simulcast.analyzer = {"simulcast_vp8_3sl_high", 0.0, 0.0, kTestDurationSec};
  simulcast.config->loss_percent = 0;
  simulcast.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  simulcast.ss[0] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, SimulcastVP8_3SL_Medium) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SimulcastVp8VideoHigh();
  simulcast.analyzer = {"simulcast_vp8_3sl_medium", 0.0, 0.0, kTestDurationSec};
  simulcast.config->loss_percent = 0;
  simulcast.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  simulcast.ss[0] = {
      streams, 1, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(simulcast);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, SimulcastVP8_3SL_Low) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging simulcast;
  simulcast.call.send_side_bwe = true;
  simulcast.video[0] = SimulcastVp8VideoHigh();
  simulcast.analyzer = {"simulcast_vp8_3sl_low", 0.0, 0.0, kTestDurationSec};
  simulcast.config->loss_percent = 0;
  simulcast.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  simulcast.ss[0] = {
      streams, 0, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(simulcast);
}

// This test assumes ideal network conditions with target bandwidth being
// available and exercises WebRTC calls with a high target bitrate(100 Mbps).
// Android32 bots can't handle this high bitrate, so disable test for those.
#if defined(WEBRTC_ANDROID)
#define MAYBE_HighBitrateWithFakeCodec DISABLED_HighBitrateWithFakeCodec
#else
#define MAYBE_HighBitrateWithFakeCodec HighBitrateWithFakeCodec
#endif  // defined(WEBRTC_ANDROID)
// TODO(bugs.webrtc.org/10639) Disabled because target bitrate can't be
configured yet. TEST(PCFullStackTest, MAYBE_HighBitrateWithFakeCodec) { auto
fixture = CreateVideoQualityTestFixture(); const int target_bitrate = 100000000;
  ParamsWithLogging generator;
  generator.call.send_side_bwe = true;
  generator.call.call_bitrate_config.min_bitrate_bps = target_bitrate;
  generator.call.call_bitrate_config.start_bitrate_bps = target_bitrate;
  generator.call.call_bitrate_config.max_bitrate_bps = target_bitrate;
  generator.video[0] = {true,
                        360,
                        240,
                        30,
                        target_bitrate / 2,
                        target_bitrate,
                        target_bitrate * 2,
                        false,
                        "FakeCodec",
                        1,
                        0,
                        0,
                        false,
                        false,
                        false,
                        "Generator"};
  generator.analyzer = {"high_bitrate_with_fake_codec", 0.0, 0.0,
                        kTestDurationSec};
  fixture->RunWithAnalyzer(generator);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, LargeRoomVP8_5thumb) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging large_room;
  large_room.call.send_side_bwe = true;
  large_room.video[0] = SimulcastVp8VideoHigh();
  large_room.analyzer = {"largeroom_5thumb", 0.0, 0.0, kTestDurationSec};
  large_room.config->loss_percent = 0;
  large_room.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  large_room.call.num_thumbnails = 5;
  large_room.ss[0] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(large_room);
}

#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS)
// Fails on mobile devices:
// https://bugs.chromium.org/p/webrtc/issues/detail?id=7301
#define MAYBE_LargeRoomVP8_50thumb DISABLED_LargeRoomVP8_50thumb
#define MAYBE_LargeRoomVP8_15thumb DISABLED_LargeRoomVP8_15thumb
#else
#define MAYBE_LargeRoomVP8_50thumb LargeRoomVP8_50thumb
#define MAYBE_LargeRoomVP8_15thumb LargeRoomVP8_15thumb
#endif
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, MAYBE_LargeRoomVP8_15thumb) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging large_room;
  large_room.call.send_side_bwe = true;
  large_room.video[0] = SimulcastVp8VideoHigh();
  large_room.analyzer = {"largeroom_15thumb", 0.0, 0.0, kTestDurationSec};
  large_room.config->loss_percent = 0;
  large_room.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  large_room.call.num_thumbnails = 15;
  large_room.ss[0] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(large_room);
}

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST(PCFullStackTest, MAYBE_LargeRoomVP8_50thumb) {
  auto fixture = CreateVideoQualityTestFixture();
  ParamsWithLogging large_room;
  large_room.call.send_side_bwe = true;
  large_room.video[0] = SimulcastVp8VideoHigh();
  large_room.analyzer = {"largeroom_50thumb", 0.0, 0.0, kTestDurationSec};
  large_room.config->loss_percent = 0;
  large_room.config->queue_delay_ms = 100;
  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();

  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};
  large_room.call.num_thumbnails = 50;
  large_room.ss[0] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};
  fixture->RunWithAnalyzer(large_room);
}
*/

INSTANTIATE_TEST_SUITE_P(
    PCFullStackTest,
    PCGenericDescriptorTest,
    ::testing::Values("WebRTC-GenericDescriptor/Disabled/",
                      "WebRTC-GenericDescriptor/Enabled/"));

class PCDualStreamsTest : public ::testing::TestWithParam<int> {};

/*
// Disable dual video test on mobile device becuase it's too heavy.
// TODO(bugs.webrtc.org/9840): Investigate why is this test flaky on MAC.
#if !defined(WEBRTC_ANDROID) && !defined(WEBRTC_IOS) && !defined(WEBRTC_MAC)
// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST_P(PCDualStreamsTest,
       ModeratelyRestricted_SlidesVp8_2TL_Simulcast_Video_Simulcast_High) {
  test::ScopedFieldTrials field_trial(
      AppendFieldTrials(std::string(kScreenshareSimulcastExperiment)));
  const int first_stream = GetParam();
  ParamsWithLogging dual_streams;

  // Screenshare Settings.
  dual_streams.screenshare[first_stream] = {true, false, 10};
  dual_streams.video[first_stream] = {true,    1850,  1110,  5, 800000, 2500000,
                                      2500000, false, "VP8", 2, 1,      400000,
                                      false,   false, false, ""};

  ParamsWithLogging screenshare_params_high;
  screenshare_params_high.video[0] = {
      true,  1850, 1110, 60,     600000, 1250000, 1250000, false,
      "VP8", 2,    0,    400000, false,  false,   false,   ""};
  VideoQualityTest::Params screenshare_params_low;
  screenshare_params_low.video[0] = {true,    1850,  1110,  5, 30000, 200000,
                                     1000000, false, "VP8", 2, 0,     400000,
                                     false,   false, false, ""};
  std::vector<VideoStream> screenhsare_streams = {
      VideoQualityTest::DefaultVideoStream(screenshare_params_low, 0),
      VideoQualityTest::DefaultVideoStream(screenshare_params_high, 0)};

  dual_streams.ss[first_stream] = {
      screenhsare_streams,         1,    1, 0, InterLayerPredMode::kOn,
      std::vector<SpatialLayer>(), false};

  // Video settings.
  dual_streams.video[1 - first_stream] = SimulcastVp8VideoHigh();

  ParamsWithLogging video_params_high;
  video_params_high.video[0] = SimulcastVp8VideoHigh();
  ParamsWithLogging video_params_medium;
  video_params_medium.video[0] = SimulcastVp8VideoMedium();
  ParamsWithLogging video_params_low;
  video_params_low.video[0] = SimulcastVp8VideoLow();
  std::vector<VideoStream> streams = {
      VideoQualityTest::DefaultVideoStream(video_params_low, 0),
      VideoQualityTest::DefaultVideoStream(video_params_medium, 0),
      VideoQualityTest::DefaultVideoStream(video_params_high, 0)};

  dual_streams.ss[1 - first_stream] = {
      streams, 2, 1, 0, InterLayerPredMode::kOn, std::vector<SpatialLayer>(),
      false};

  // Call settings.
  dual_streams.call.send_side_bwe = true;
  dual_streams.call.dual_video = true;
  std::string test_label = "dualstreams_moderately_restricted_screenshare_" +
                           std::to_string(first_stream);
  dual_streams.analyzer = {test_label, 0.0, 0.0, kTestDurationSec};
  dual_streams.config->loss_percent = 1;
  dual_streams.config->link_capacity_kbps = 7500;
  dual_streams.config->queue_length_packets = 30;
  dual_streams.config->queue_delay_ms = 100;

  auto fixture = CreateVideoQualityTestFixture();
  fixture->RunWithAnalyzer(dual_streams);
}
#endif  // !defined(WEBRTC_ANDROID) && !defined(WEBRTC_IOS) &&
        // !defined(WEBRTC_MAC)

// TODO(bugs.webrtc.org/10639) requires simulcast/SVC support in PC framework
TEST_P(PCDualStreamsTest, Conference_Restricted) {
  const int first_stream = GetParam();
  ParamsWithLogging dual_streams;

  // Screenshare Settings.
  dual_streams.screenshare[first_stream] = {true, false, 10};
  dual_streams.video[first_stream] = {true,    1850,  1110,  5, 800000, 2500000,
                                      2500000, false, "VP8", 3, 2,      400000,
                                      false,   false, false, ""};
  // Video settings.
  dual_streams.video[1 - first_stream] = {
      true,   1280,
      720,    30,
      150000, 500000,
      700000, false,
      "VP8",  3,
      2,      400000,
      false,  false,
      false,  ClipNameToClipPath("ConferenceMotion_1280_720_50")};

  // Call settings.
  dual_streams.call.send_side_bwe = true;
  dual_streams.call.dual_video = true;
  std::string test_label = "dualstreams_conference_restricted_screenshare_" +
                           std::to_string(first_stream);
  dual_streams.analyzer = {test_label, 0.0, 0.0, kTestDurationSec};
  dual_streams.config->loss_percent = 1;
  dual_streams.config->link_capacity_kbps = 5000;
  dual_streams.config->queue_length_packets = 30;
  dual_streams.config->queue_delay_ms = 100;

  auto fixture = CreateVideoQualityTestFixture();
  fixture->RunWithAnalyzer(dual_streams);
}
*/

INSTANTIATE_TEST_SUITE_P(PCFullStackTest,
                         PCDualStreamsTest,
                         ::testing::Values(0, 1));

}  // namespace webrtc
