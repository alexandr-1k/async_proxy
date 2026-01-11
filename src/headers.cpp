#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

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

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::string host;
    std::string port = "80";
    auto host_port_callback = [&host, &port](std::string_view key, std::string_view value) {
        if (key != "Host"sv) {
            return;
        }
        size_t colon_pos = value.find(':');
        if (colon_pos != std::string_view::npos) {
            host = std::string(value.substr(0, colon_pos));
            port = std::string(value.substr(colon_pos + 1));
        } else {
            host = std::string(value);
        }
    };
    iterHeaders(req, host_port_callback);
    return {host, port};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> content_length;
    auto content_length_callback = [&content_length](std::string_view key, std::string_view value) {
        if (key != "Content-Length"sv) {
            return;
        }
        try {
            content_length = std::stoul(std::string(value));
        } catch (...) {
            // ignore invalid content-length
        }
    };
    iterHeaders(rsp, content_length_callback);
    return content_length;
}
