#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;


void iterHeaders(std::string_view req, Callback&& callback) {
  // code here
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
  // code here
}

std::optional<size_t> findContentLength(std::string_view rsp) {
  // code here
}
