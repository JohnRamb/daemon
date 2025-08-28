#include "s_expression_parser.h"
#include <sstream>

std::vector<std::string> SExpressionParser::parseCommand(const std::string& command) {
    std::vector<std::string> result;
    std::string current;
    int depth = 0;
    bool in_token = false;

    // Проверяем, что команда начинается с '(' и заканчивается ')'
    if (command.length() < 2 || command.front() != '(' || command.back() != ')') {
        return result; // Пустой результат при неверном формате
    }

    // Парсим содержимое между скобками
    for (size_t i = 1; i < command.length() - 1; ++i) {
        char c = command[i];
        if (c == '(') {
            if (depth == 0 && !current.empty()) {
                result.push_back(current);
                current.clear();
            }
            depth++;
            in_token = false;
        } else if (c == ')') {
            depth--;
            if (depth == 0 && !current.empty()) {
                result.push_back(current);
                current.clear();
            }
            in_token = false;
        } else if (c == ',' && depth == 0 && !in_token) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else if (c != ' ' && c != '\t' && c != '\n') {
            current += c;
            in_token = true;
        } else if (in_token && (c == ' ' || c == '\t' || c == '\n')) {
            in_token = false;
        }
    }

    if (!current.empty() && depth == 0) {
        result.push_back(current);
    }

    return result;
}

std::string SExpressionParser::serializeResponse(const std::string& command, const std::string& response) {
    std::stringstream ss;
    ss << "(" << command << "(" << response << "))";
    return ss.str();
}