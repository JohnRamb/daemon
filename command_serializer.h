#ifndef COMMAND_SERIALIZER_H
#define COMMAND_SERIALIZER_H

#include <string>
#include <vector>

class CommandSerializer {
public:
    virtual ~CommandSerializer() = default;
    
    // Парсит входную команду и возвращает вектор токенов (команда + аргументы)
    virtual std::vector<std::string> parseCommand(const std::string& command) = 0;
    
    // Сериализует ответ в требуемый формат (например, S-выражение)
    virtual std::string serializeResponse(const std::string& command, const std::string& response) = 0;
};

#endif // COMMAND_SERIALIZER_H