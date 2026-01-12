#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>

using Callback = std::function<void(std::string_view, std::string_view)>;
enum class HeaderError {
    missing,
    parse_error,
    out_of_range,
};
using ContentLengthResponse = std::expected<std::optional<size_t>, HeaderError>;
using HostPortPair = std::pair<std::string, std::string>;
using HostResponse = std::expected<HostPortPair, HeaderError>;

void iterHeaders(std::string_view req, Callback &&callback);

HostResponse findHostPort(std::string_view req);

ContentLengthResponse findContentLength(std::string_view rsp);
