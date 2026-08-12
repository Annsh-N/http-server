#include "http/fd.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <type_traits>
#include <unistd.h>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool is_closed(int fd) {
    errno = 0;
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

void make_pipe(int fds[2]) {
    if (pipe(fds) != 0) {
        perror("pipe");
        std::exit(1);
    }
}

void test_destructor_closes_owned_fd() {
    int fds[2];
    make_pipe(fds);

    {
        http::Fd read_end(fds[0]);
        expect(read_end.valid(), "owned fd should be valid");
    }

    expect(is_closed(fds[0]), "destructor should close owned fd");
    close(fds[1]);
}

void test_move_constructor_transfers_ownership() {
    int fds[2];
    make_pipe(fds);

    {
        http::Fd original(fds[0]);
        http::Fd moved(std::move(original));

        expect(!original.valid(), "moved-from fd should be invalid");
        expect(moved.get() == fds[0], "moved fd should keep original value");
        expect(!is_closed(fds[0]), "moved fd should remain open");
    }

    expect(is_closed(fds[0]), "moved owner should close fd");
    close(fds[1]);
}

void test_move_assignment_closes_previous_fd() {
    int first[2];
    int second[2];
    make_pipe(first);
    make_pipe(second);

    {
        http::Fd owner(first[0]);
        http::Fd replacement(second[0]);

        owner = std::move(replacement);

        expect(is_closed(first[0]),
               "move assignment should close previous owned fd");
        expect(!replacement.valid(),
               "move-assigned-from fd should be invalid");
        expect(owner.get() == second[0],
               "move assignment should take replacement fd");
    }

    expect(is_closed(second[0]), "move-assigned owner should close new fd");
    close(first[1]);
    close(second[1]);
}

void test_release_prevents_close() {
    int fds[2];
    make_pipe(fds);

    int raw = -1;
    {
        http::Fd owner(fds[0]);
        raw = owner.release();

        expect(raw == fds[0], "release should return raw fd");
        expect(!owner.valid(), "released owner should be invalid");
    }

    expect(!is_closed(raw), "released fd should remain open");
    close(raw);
    close(fds[1]);
}

void test_reset_replaces_fd() {
    int first[2];
    int second[2];
    make_pipe(first);
    make_pipe(second);

    {
        http::Fd owner(first[0]);
        owner.reset(second[0]);

        expect(is_closed(first[0]), "reset should close previous fd");
        expect(owner.get() == second[0], "reset should own replacement fd");
    }

    expect(is_closed(second[0]), "reset replacement should close on destroy");
    close(first[1]);
    close(second[1]);
}

static_assert(!std::is_copy_constructible_v<http::Fd>);
static_assert(!std::is_copy_assignable_v<http::Fd>);
static_assert(std::is_move_constructible_v<http::Fd>);
static_assert(std::is_move_assignable_v<http::Fd>);

} // namespace

int main() {
    test_destructor_closes_owned_fd();
    test_move_constructor_transfers_ownership();
    test_move_assignment_closes_previous_fd();
    test_release_prevents_close();
    test_reset_replaces_fd();

    if (failures != 0) {
        std::cerr << failures << " fd test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "fd_tests passed\n";
    return 0;
}

