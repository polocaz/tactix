#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "spdlog/spdlog.h"

// The suite emits thousands of spdlog [info] lines from Simulation.cpp during normal
// operation, which buries the doctest summary -- the one thing anyone actually reads in a
// CI log. Silence spdlog for this binary only; logging in src/ is untouched.
int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::off);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
