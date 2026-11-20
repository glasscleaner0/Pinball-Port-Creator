// Native execution of the newly written portable runtime tests. No game assets
// or supplied source code are compiled into this executable.
#define main pinball_runtime_test_entry
#include "../../tests/rewrite/runtime_tests.cpp"
#undef main
#include <xsdk/log.h>
int main() {
    xsdk_log_write("self-test: starting portable runtime boundaries on PowerPC\n");
    const int result = pinball_runtime_test_entry();
    xsdk_log_write("self-test: runtime result %ld\n", static_cast<long>(result));
    return result;
}
