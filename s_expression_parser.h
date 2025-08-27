#ifndef S_EXPRESSION_PARSER_H
#define S_EXPRESSION_PARSER_H

#include "command_serializer.h"
#include <string>
#include <vector>

class SExpressionParser : public CommandSerializer {
public:
    std::vector<std::string> parseCommand(const std::string& command) override;
    std::string serializeResponse(const std::string& command, const std::string& response) override;
};

#endif // S_EXPRESSION_PARSER_H