#include "headers.h"
#include <gtest/gtest.h>

using namespace std::string_view_literals;

TEST(iterHeaders, Empty) {
    auto callback = [](std::string_view key, std::string_view value) {
        FAIL() << "Callback should not be called for empty request";
    };
    iterHeaders(""sv, callback);
}

TEST(iterHeaders, SkipRequestLine) {
    std::string_view request = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "\r\n";

    bool called = false;
    auto callback = [&called](std::string_view key, std::string_view value) {
        called = true;
        EXPECT_EQ(key, "Content-Type"sv);
        EXPECT_EQ(value, "text/html"sv);
    };

    iterHeaders(request, callback);
    EXPECT_TRUE(called) << "Callback should be called for headers";
}

TEST(iterHeaders, SingleHeader) {
    std::string_view request = "HTTP/1.1 200 OK\r\n"
                               "Content-Length: 1234\r\n"
                               "\r\n";

    bool called = false;
    auto callback = [&called](std::string_view key, std::string_view value) {
        called = true;
        EXPECT_EQ(key, "Content-Length"sv);
        EXPECT_EQ(value, "1234"sv);
    };

    iterHeaders(request, callback);
    EXPECT_TRUE(called) << "Callback should be called for single header";
}

TEST(iterHeaders, MultipleHeaders) {
    std::string_view request = "HTTP/1.1 200 OK\r\n"
                               "Host: example.com\r\n"
                               "Content-Type: text/html\r\n"
                               "Content-Length: 5678\r\n"
                               "\r\n";

    std::vector<std::pair<std::string_view, std::string_view>> expected = {
        {"Host"sv, "example.com"sv}, {"Content-Type"sv, "text/html"sv}, {"Content-Length"sv, "5678"sv}};

    size_t index = 0;
    auto callback = [&index, &expected](std::string_view key, std::string_view value) {
        ASSERT_LT(index, expected.size()) << "More headers than expected";
        EXPECT_EQ(key, expected[index].first);
        EXPECT_EQ(value, expected[index].second);
        index++;
    };

    iterHeaders(request, callback);
    EXPECT_EQ(index, expected.size()) << "Fewer headers than expected";
}

TEST(iterHeaders, MultipleSameHeaders) {
    std::string_view request = "HTTP/1.1 200 OK\r\n"
                               "Set-Cookie: id=123\r\n"
                               "Set-Cookie: token=abc\r\n"
                               "\r\n";

    std::vector<std::pair<std::string_view, std::string_view>> expected = {{"Set-Cookie"sv, "id=123"sv},
                                                                           {"Set-Cookie"sv, "token=abc"sv}};

    size_t index = 0;
    auto callback = [&index, &expected](std::string_view key, std::string_view value) {
        ASSERT_LT(index, expected.size()) << "More headers than expected";
        EXPECT_EQ(key, expected[index].first);
        EXPECT_EQ(value, expected[index].second);
        index++;
    };

    iterHeaders(request, callback);
    EXPECT_EQ(index, expected.size()) << "Fewer headers than expected";
}

TEST(findHostPort, Simple) {
    std::string_view request = "GET / HTTP/1.1\r\n"
                               "Host: example.com:8080\r\n"
                               "\r\n";

    auto [host, port] = findHostPort(request);
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, NoHost) {
    std::string_view request = "GET / HTTP/1.1\r\n"
                               "User-Agent: TestAgent\r\n"
                               "\r\n";

    auto [host, port] = findHostPort(request);
    EXPECT_EQ(host, "");
    EXPECT_EQ(port, "80");
}

TEST(findContentLength, Simple) {
    std::string_view response = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 4321\r\n"
                                "\r\n";

    auto content_length = findContentLength(response);
    ASSERT_TRUE(content_length.has_value());
    EXPECT_EQ(content_length.value(), 4321);
}

TEST(findContentLength, NoContentLength) {
    std::string_view response = "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html\r\n"
                                "\r\n";

    auto content_length = findContentLength(response);
    EXPECT_FALSE(content_length.has_value());
}
