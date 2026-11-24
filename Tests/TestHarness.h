#pragma once
// A tiny test harness: TEST(name) { CHECK(cond); ... } and a main() that runs them all.
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pnamtest
{
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

inline int runAll(int argc, char** argv)
{
    int failed = 0, run = 0;
    const std::string filter = argc > 1 ? argv[1] : "";
    for (auto& c : registry())
    {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
            continue;
        ++run;
        std::printf("[ RUN  ] %s\n", c.name);
        std::fflush(stdout);
        try
        {
            c.fn();
            std::printf("[  OK  ] %s\n", c.name);
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::printf("[ FAIL ] %s: %s\n", c.name, e.what());
        }
        std::fflush(stdout);
    }
    std::printf("%d/%d tests passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
} // namespace pnamtest

#define PNAM_CAT2(a, b) a##b
#define PNAM_CAT(a, b) PNAM_CAT2(a, b)
#define TEST(name)                                                                     \
    static void PNAM_CAT(test_, name)();                                               \
    static pnamtest::Reg PNAM_CAT(reg_, name)(#name, PNAM_CAT(test_, name));           \
    static void PNAM_CAT(test_, name)()
#define CHECK(cond)                                                                    \
    do { if (!(cond)) throw pnamtest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK(" #cond ")"); } while (0)
#define CHECK_MSG(cond, msg)                                                           \
    do { if (!(cond)) throw pnamtest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " + (msg)); } while (0)
#define CHECK_NEAR(a, b, tol)                                                          \
    do { const double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol)))          \
        throw pnamtest::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #a "=" + std::to_string(_a) + " vs " #b "=" + std::to_string(_b)); } while (0)
