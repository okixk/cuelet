#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

int RunCueletQualitySuite();
int RunCueletBoundedTone(
    double frequency,
    double activeSeconds,
    std::filesystem::path const& outputDirectory);
int RunCueletStageEFixture(
    std::wstring_view fixtureName,
    std::filesystem::path const& outputDirectory);
int RunCueletFormatMatrix(
    std::filesystem::path const& outputDirectory);
int RunCueletCaptureSample(
    double durationSeconds,
    std::filesystem::path const& outputDirectory);
int RunCueletStressPhase(
    std::wstring_view phase,
    std::uint32_t iterations,
    std::filesystem::path const& outputDirectory);
int RunCueletStageESoak(
    double durationSeconds,
    std::filesystem::path const& outputDirectory);
