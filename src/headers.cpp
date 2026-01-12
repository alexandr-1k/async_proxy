#include "headers.h"

#include <cctype>
#include <expected>
#include <iostream>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

constexpr auto MAX_PORT_NUM = 2 << 15;
constexpr auto MAX_ALLOWED_BODY_SIZE = 2 << 20;  // 1 MB , arbitrary

void iterHeaders(std::string_view req, Callback &&callback) {
    auto first_newline = std::ranges::find(req, '\n');
    if (first_newline == req.end())
        return;
    size_t start = static_cast<size_t>(std::distance(req.begin(), first_newline)) + 1;
    if (start >= req.size())
        return;

    std::string_view headers_part = req.substr(start);

    auto subrange_to_sv = [&](auto subrange) -> std::string_view {
        if (subrange.begin() == subrange.end()) {
            return ""sv;
        }
        auto b = &*subrange.begin();
        auto e = &*subrange.end();
        auto len = static_cast<size_t>(std::distance(subrange.begin(), subrange.end()));
        return std::string_view(b, len);
    };

    for (auto line_sub : headers_part | std::views::split('\n')) {
        std::string_view line = subrange_to_sv(line_sub);
        if (line.size() && line.back() == '\r') {
            // strip trailing '\r' if present
            line.remove_suffix(1);
        }

        if (line.empty()) {
            break;  // empty line -> end of headers
        }

        auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }

        std::string_view key = line.substr(0, colon);
        while (!key.empty() && key.back() == ' ') {
            key.remove_suffix(1);
        }

        std::string_view value = line.substr(colon + 1);
        auto first_non_space = value.find_first_not_of(' ');  // remove spaces at the start of value
        if (first_non_space != std::string_view::npos) {
            value.remove_prefix(first_non_space);
        } else {
            value = ""sv;  // all-space value
        }

        callback(key, value);
    }
}

std::string to_lower_str(std::string_view s) {
    return s | std::views::transform([](char c) { return std::tolower(c); }) | std::ranges::to<std::string>();
}

HostResponse findHostPort(std::string_view req) {
    HostResponse resp{std::unexpected(HeaderError::missing)};

    auto host_port_callback = [&resp](std::string_view key, std::string_view value) {
        if (to_lower_str(key) != "host"sv) {
            return;
        }
        size_t colon_pos = value.find(':');
        std::string host, port{"80"};
        if (colon_pos != std::string_view::npos) {
            host = std::string(value.substr(0, colon_pos));

            auto port_substr = value.substr(colon_pos + 1);
            auto port_num = 0;
            try {
                port_num = std::stoi(std::string(port_substr));
            } catch (...) {
                resp = std::unexpected(HeaderError::parse_error);
                return;
            }
            if (port_num > MAX_PORT_NUM || port_num < 0) {
                resp = std::unexpected(HeaderError::out_of_range);
                return;
            }

            port = std::string{port_substr};
        } else {
            host = std::string(value);
        }

        if (host.empty() || port.empty()) {
            return;
        }
        resp = std::make_pair(host, port);
    };
    iterHeaders(req, host_port_callback);
    return resp;
}

ContentLengthResponse findContentLength(std::string_view rsp) {
    ContentLengthResponse content_length{0};
    auto content_length_callback = [&content_length](std::string_view key, std::string_view value) -> void {
        if (to_lower_str(key) != "content-length"sv) {
            return;
        }
        size_t content_length_parsed{0};
        try {
            content_length_parsed = std::stoul(std::string(value));
        } catch (...) {
            content_length = std::unexpected(HeaderError::parse_error);
            return;
        }
        if (content_length_parsed > MAX_ALLOWED_BODY_SIZE) {
            content_length = std::unexpected(HeaderError::out_of_range);
            return;
        }
        content_length = content_length_parsed;
    };
    iterHeaders(rsp, content_length_callback);
    return content_length;
}
