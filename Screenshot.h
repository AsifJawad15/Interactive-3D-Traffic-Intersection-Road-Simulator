#pragma once

#include <string>

// Reads the current back buffer and writes it as a PNG. Used by the --capture
// command-line mode so every phase of the project can be checked with a
// reproducible image of a fixed view at a fixed time of day.
bool saveFramebufferPng(const std::string& path, int width, int height);
