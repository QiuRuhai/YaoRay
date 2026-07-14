#include "pbrt_tokenizer_internal.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <utility>

namespace yr::pbrt_parse {
namespace {

bool SplitParamName(std::string_view token, std::string& type, std::string& name) {
    const std::size_t space = token.find(' ');
    if (space == std::string_view::npos || space + 1 >= token.size()) return false;
    type = std::string{token.substr(0, space)};
    name = std::string{token.substr(space + 1)};
    return true;
}

std::vector<std::string> ReadValueList(
    const std::vector<std::string>& tokens,
    std::size_t& index
) {
    std::vector<std::string> values;
    if (index < tokens.size() && tokens[index] == "[") {
        ++index;
        while (index < tokens.size() && tokens[index] != "]") {
            values.push_back(tokens[index++]);
        }
        if (index < tokens.size()) ++index;
    } else if (index < tokens.size()) {
        values.push_back(tokens[index++]);
    }
    return values;
}

template <typename Value, typename Parser>
std::optional<Value> ParseNumber(std::string_view token, Parser parser) {
    try {
        const std::string value{token};
        std::size_t consumed = 0;
        const Value parsed = parser(value, consumed);
        return consumed == value.size() ? std::optional<Value>{parsed} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> ParseFloatValue(std::string_view token) {
    return ParseNumber<float>(token, [](const std::string& value, std::size_t& consumed) {
        return std::stof(value, &consumed);
    });
}

std::optional<int> ParseInt(std::string_view token) {
    return ParseNumber<int>(token, [](const std::string& value, std::size_t& consumed) {
        return std::stoi(value, &consumed);
    });
}

bool IsFloatParam(std::string_view type) {
    return type == "float" || type == "rgb" || type == "color" ||
           type == "point3" || type == "point2" || type == "vector3" ||
           type == "normal" || type == "blackbody";
}

void PopulateTypedValues(PbrtParam& param, std::vector<std::string> values) {
    if (IsFloatParam(param.type)) {
        for (const std::string& value : values) {
            param.floats.push_back(ParseFloatValue(value).value_or(0.0f));
        }
        return;
    }
    if (param.type == "spectrum") {
        if (!values.empty() && !ParseFloatValue(values.front()).has_value()) {
            param.strings = std::move(values);
        } else {
            for (const std::string& value : values) {
                param.floats.push_back(ParseFloatValue(value).value_or(0.0f));
            }
        }
        return;
    }
    if (param.type == "integer") {
        for (const std::string& value : values) {
            param.ints.push_back(ParseInt(value).value_or(0));
        }
        return;
    }
    if (param.type == "bool") {
        for (const std::string& value : values) {
            param.bools.push_back(value == "true" || value == "1");
        }
        return;
    }
    param.strings = std::move(values);
}

} // namespace

std::optional<float> ParseFloatToken(std::string_view token) {
    return ParseFloatValue(token);
}

std::vector<std::string> Tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < text.size();) {
        const char current = text[i];
        if (std::isspace(static_cast<unsigned char>(current))) {
            ++i;
            continue;
        }
        if (current == '#') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (current == '[' || current == ']') {
            tokens.emplace_back(1, current);
            ++i;
            continue;
        }
        if (current == '"') {
            ++i;
            std::string value;
            while (i < text.size() && text[i] != '"') value.push_back(text[i++]);
            if (i < text.size()) ++i;
            tokens.push_back(std::move(value));
            continue;
        }

        std::string value;
        while (i < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[i])) &&
               text[i] != '[' && text[i] != ']' && text[i] != '#') {
            value.push_back(text[i++]);
        }
        tokens.push_back(std::move(value));
    }
    return tokens;
}

std::vector<PbrtParam> ReadParams(
    const std::vector<std::string>& tokens,
    std::size_t& index
) {
    std::vector<PbrtParam> params;
    while (index < tokens.size()) {
        PbrtParam param;
        if (!SplitParamName(tokens[index], param.type, param.name)) break;
        ++index;
        PopulateTypedValues(param, ReadValueList(tokens, index));
        params.push_back(std::move(param));
    }
    return params;
}

const PbrtParam* FindParam(
    const std::vector<PbrtParam>& params,
    std::string_view name
) {
    for (const PbrtParam& param : params) {
        if (param.name == name) return &param;
    }
    return nullptr;
}

std::string StringParam(
    const std::vector<PbrtParam>& params,
    std::string_view name,
    const std::string& fallback
) {
    const PbrtParam* param = FindParam(params, name);
    return param == nullptr || param->strings.empty() ? fallback : param->strings.front();
}

} // namespace yr::pbrt_parse
