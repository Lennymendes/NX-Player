// NX Player — MIT License
// Minimal self-test framework: sections register themselves, `NX Player --selftest [filtre]` runs them.
#pragma once

#include <juce_core/juce_core.h>
#include "../Source/Core/Text.h"
#include <functional>
#include <iostream>
#include <vector>

namespace nxtest
{

struct Context
{
    int passed = 0, failed = 0;
    juce::String section;
    juce::File tempDir;

    void check (bool ok, const juce::String& what)
    {
        if (ok) ++passed;
        else ++failed;
        std::cout << (ok ? "  [OK]   " : "  [ÉCHEC] ") << what.toStdString() << std::endl;
    }

    void info (const juce::String& s) { std::cout << "         " << s.toStdString() << std::endl; }
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
