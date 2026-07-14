#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yr {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct SceneDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::filesystem::path file;
    std::string field;
    std::string message;
};

bool HasSceneErrors(const std::vector<SceneDiagnostic>& diagnostics);
std::string FormatSceneDiagnostic(const SceneDiagnostic& diagnostic);
std::string FormatSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics);

} // namespace yr
