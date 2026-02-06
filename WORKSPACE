workspace(name = "market_data_feedhandler")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# C++ rules
http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz"],
    sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
    strip_prefix = "rules_cc-0.0.9",
)

# Abseil
http_archive(
    name = "com_google_absl",
    urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.2.tar.gz"],
    strip_prefix = "abseil-cpp-20240116.2",
    sha256 = "733726b8c3a6d39a4120d7e45ea8b41a434cdacde401cba500f14236c49b39dc",
)

# yaml-cpp for config
http_archive(
    name = "yaml_cpp",
    urls = ["https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz"],
    strip_prefix = "yaml-cpp-0.8.0",
    sha256 = "fbe74bbdcee21d656715688f1d7d7557e1084f5f7df8ac1e9e3f6eff4faece52",
)

# Google Test
http_archive(
    name = "com_google_googletest",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    strip_prefix = "googletest-1.14.0",
    sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
)
