// NX Player — MIT License
// Minimal self-test framework: sections register themselves, `NX Player --selftest [filtre]` runs them.
#pragma once

#include <juce_core/juce_core.h>
#include "../Source/Core/Text.h"
#include <functional>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace nxtest
{

/** Writes a line to stdout and, when NX_SELFTEST_LOG names a file, to that file too (flushed per line,
    so the log survives a crash; used by CI where a GUI-subsystem exe's console output is hard to capture). */
inline void emit (const std::string& line)
{
    static std::ofstream file = []
    {
        std::ofstream f;
        if (const char* path = std::getenv ("NX_SELFTEST_LOG"))
            f.open (path, std::ios::out | std::ios::trunc | std::ios::binary);
        return f;
    }();
    std::cout << line << std::endl;
    if (file.is_open()) file << line << "\n" << std::flush;
}

struct Context
{
    int passed = 0, failed = 0;
    juce::String section;
    juce::File tempDir;

    void check (bool ok, const juce::String& what)
    {
        if (ok) ++passed;
        else ++failed;
        emit ((ok ? "  [OK]   " : "  [ÉCHEC] ") + what.toStdString());
    }

    void info (const juce::String& s) { emit ("         " + s.toStdString()); }
};

struct Section
{
    int order;
    const char* name;
    std::function<void (Context&)> fn;
};

inline std::vector<Section>& registry()
{
    static std::vector<Section> r;
    return r;
}

struct Registrar
{
    Registrar (int order, const char* name, std::function<void (Context&)> fn) { registry().push_back ({ order, name, std::move (fn) }); }
};

/** Runs every section whose name contains filter (empty = all). Returns the number of failures. */
int runAll (const juce::String& filter);

} // namespace nxtest

#define NX_TEST_CONCAT2(a, b) a##b
#define NX_TEST_CONCAT(a, b) NX_TEST_CONCAT2(a, b)
#define NX_TEST_SECTION(order, name) \
    static void NX_TEST_CONCAT(nxTestFn_, order) (nxtest::Context& t); \
    static nxtest::Registrar NX_TEST_CONCAT(nxTestReg_, order) (order, name, &NX_TEST_CONCAT(nxTestFn_, order)); \
    static void NX_TEST_CONCAT(nxTestFn_, order) (nxtest::Context& t)
