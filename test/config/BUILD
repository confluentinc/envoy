licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "utility_lib",
    srcs = [
        "utility.cc",
    ],
    hdrs = [
        "utility.h",
    ],
    deps = [
        "//source/common/config:resources_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//test/config/integration/certs:hashes",
        "//test/integration:server_stats_interface",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/config/accesslog/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/transport_socket/tap/v2alpha:pkg_cc_proto",
    ],
)
