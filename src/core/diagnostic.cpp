#include <yaoray/core/diagnostic.hpp>

#include <sstream>

namespace yr {

bool HasSceneErrors(const std::vector<SceneDiagnostic>& diagnostics) {
    for (const SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

std::string FormatSceneDiagnostic(const SceneDiagnostic& diagnostic) {
    std::ostringstream out;
    out << (diagnostic.severity == DiagnosticSeverity::Error ? "Scene error" : "Scene warning");
    if (!diagnostic.file.empty()) {
        out << " in " << diagnostic.file.generic_string();
    }
    out << ":\n";
    if (!diagnostic.field.empty()) {
        out << "  [" << diagnostic.field << "] ";
    } else {
        out << "  ";
    }
    out << diagnostic.message;
    return out.str();
}

std::string FormatSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics) {
    std::ostringstream out;
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out << '\n';
        }
        out << FormatSceneDiagnostic(diagnostics[i]);
    }
    return out.str();
}

} // namespace yr
