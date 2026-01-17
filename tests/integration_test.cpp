#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static pid_t start_backend_server() {
    int pipefd[2];
    pipe(pipefd);

    pid_t py = fork();

    if (py == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        execlp("python3", "python3", "-c",
               "print(\"HTTP/1.1 200 OK\\r\\n"
               "Content-Type: text/html\\r\\n"
               "Content-Length: 4096\\r\\n"
               "\\r\\n\" + \"A\"*4096, end=\"\")",
               nullptr);
        _exit(127);
    }

    pid_t nc = fork();

    if (nc == 0) {
        // pipe read -> nc stdin
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[1]);
        close(pipefd[0]);

        execlp("nc", "nc", "-l", "127.0.0.1", "-p", "8000", nullptr);
        _exit(127);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    return nc;
}

static pid_t start_proxy() {
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    std::string proxy_path = std::string(cwd) + "/AsyncHttpProxy";
    access(proxy_path.c_str(), X_OK);

    pid_t pid = fork();

    if (pid == 0) {
        execl(proxy_path.c_str(), proxy_path.c_str(), "5555", nullptr);
        _exit(127);
    }
    return pid;
}

static std::string run_wget() {
    std::array<char, 4096> buf{};
    std::string output;

    FILE *f = popen("wget -qO- -e use_proxy=yes -e http_proxy=127.0.0.1:5555 127.0.0.1:8000", "r");
    EXPECT_NE(f, nullptr);

    while (!feof(f)) {
        size_t n = fread(buf.data(), 1, buf.size(), f);
        output.append(buf.data(), n);
    }

    pclose(f);
    return output;
}

TEST(AsyncHttpProxyIntegration, ForwardsBodyExactly) {
    pid_t backend = start_backend_server();
    pid_t proxy = start_proxy();

    // Give processes time to start
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string output = run_wget();

    std::string expected(4096, 'A');
    ASSERT_EQ(output.size(), expected.size());
    ASSERT_EQ(output, expected);

    kill(proxy, SIGTERM);
    kill(backend, SIGTERM);

    waitpid(proxy, nullptr, 0);
    waitpid(backend, nullptr, 0);
}
