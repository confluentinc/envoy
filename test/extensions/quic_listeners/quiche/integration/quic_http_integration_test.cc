#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class CodecClientCallbacksForTest : public Http::CodecClientCallbacks {
public:
  void onStreamDestroy() override {}

  void onStreamReset(Http::StreamResetReason reason) override {
    last_stream_reset_reason_ = reason;
  }

  Http::StreamResetReason last_stream_reset_reason_{Http::StreamResetReason::LocalReset};
};

class QuicHttpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  QuicHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP3, GetParam(),
                            ConfigHelper::QUIC_HTTP_PROXY_CONFIG),
        supported_versions_(quic::CurrentSupportedVersions()),
        crypto_config_(std::make_unique<EnvoyQuicFakeProofVerifier>()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()) {}

  Network::ClientConnectionPtr makeClientConnection(uint32_t port) override {
    Network::Address::InstanceConstSharedPtr server_addr = Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Address::InstanceConstSharedPtr local_addr =
        Network::Test::getCanonicalLoopbackAddress(version_);
    // Initiate a QUIC connection with the highest supported version. If not
    // supported by server, this connection will fail.
    // TODO(danzh) Implement retry upon version mismatch and modify test frame work to specify a
    // different version set on server side to test that.
    auto connection = std::make_unique<EnvoyQuicClientConnection>(
        getNextServerDesignatedConnectionId(), server_addr, conn_helper_, alarm_factory_,
        quic::ParsedQuicVersionVector{supported_versions_[0]}, local_addr, *dispatcher_, nullptr);
    auto session = std::make_unique<EnvoyQuicClientSession>(
        quic_config_, supported_versions_, std::move(connection), server_id_, &crypto_config_,
        &push_promise_index_, *dispatcher_, 0);
    session->Initialize();
    return session;
  }

  // This call may fail because of INVALID_VERSION, because QUIC connection doesn't support
  // in-connection version negotiation.
  // TODO(#8479) Propagate INVALID_VERSION error to caller and let caller to use server advertised
  // version list to create a new connection with mutually supported version and make client codec
  // again.
  IntegrationCodecClientPtr makeRawHttpConnection(Network::ClientConnectionPtr&& conn) override {
    IntegrationCodecClientPtr codec = HttpIntegrationTest::makeRawHttpConnection(std::move(conn));
    if (codec->disconnected()) {
      // Connection may get closed during version negotiation or handshake.
      ENVOY_LOG(error, "Fail to connect to server with error: {}",
                codec->connection()->transportFailureReason());
    } else {
      codec->setCodecClientCallbacks(client_codec_callback_);
    }
    return codec;
  }

  quic::QuicConnectionId getNextServerDesignatedConnectionId() {
    quic::QuicCryptoClientConfig::CachedState* cached = crypto_config_.LookupOrCreate(server_id_);
    // If the cached state indicates that we should use a server-designated
    // connection ID, then return that connection ID.
    quic::QuicConnectionId conn_id = cached->has_server_designated_connection_id()
                                         ? cached->GetNextServerDesignatedConnectionId()
                                         : quic::EmptyQuicConnectionId();
    return conn_id.IsEmpty() ? quic::QuicUtils::CreateRandomConnectionId() : conn_id;
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      envoy::api::v2::auth::DownstreamTlsContext tls_context;
      ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context());
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* transport_socket = filter_chain->mutable_transport_socket();
      TestUtility::jsonConvert(tls_context, *transport_socket->mutable_config());
    });
    config_helper_.addConfigModifier(
        [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
               hcm) {
          hcm.mutable_delayed_close_timeout()->set_nanos(0);
          EXPECT_EQ(hcm.codec_type(), envoy::config::filter::network::http_connection_manager::v2::
                                          HttpConnectionManager::HTTP3);
        });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

protected:
  quic::QuicConfig quic_config_;
  quic::QuicServerId server_id_{"example.com", 443, false};
  quic::QuicClientPushPromiseIndex push_promise_index_;
  quic::ParsedQuicVersionVector supported_versions_;
  quic::QuicCryptoClientConfig crypto_config_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  CodecClientCallbacksForTest client_codec_callback_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicHttpIntegrationTest, GetRequestAndEmptyResponse) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, GetRequestAndResponseWithBody) {
  initialize();
  sendRequestAndVerifyResponse(default_request_headers_, /*request_size=*/0,
                               default_response_headers_, /*response_size=*/1024,
                               /*backend_index*/ 0);
}

TEST_P(QuicHttpIntegrationTest, PostRequestAndResponseWithBody) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(QuicHttpIntegrationTest, PostRequestWithBigHeadersAndResponseWithBody) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
  EXPECT_EQ(Http::StreamResetReason::RemoteReset, client_codec_callback_.last_stream_reset_reason_);
}

TEST_P(QuicHttpIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, Retry) { testRetry(); }

TEST_P(QuicHttpIntegrationTest, UpstreamReadDisabledOnGiantResponseBody) {
  config_helper_.setBufferLimits(/*upstream_buffer_limit=*/1024, /*downstream_buffer_limit=*/1024);
  testRouterRequestAndResponseWithBody(/*request_size=*/512, /*response_size=*/1024 * 1024, false);
}

TEST_P(QuicHttpIntegrationTest, DownstreamReadDisabledOnGiantPost) {
  config_helper_.setBufferLimits(/*upstream_buffer_limit=*/1024, /*downstream_buffer_limit=*/1024);
  testRouterRequestAndResponseWithBody(/*request_size=*/1024 * 1024, /*response_size=*/1024, false);
}

} // namespace Quic
} // namespace Envoy
