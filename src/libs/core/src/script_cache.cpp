#include "script_cache.h"

storm::script_cache::ReaderException::ReaderException()
    : std::runtime_error("Unable to read binary data. Scripts mismatch?")
{
}

storm::script_cache::BufferReader::BufferReader(std::vector<char> &&buffer)
    : buffer_(std::move(buffer)), cur_pointer_(0)
{
}

std::string_view storm::script_cache::BufferReader::ReadArray()
{
    const auto buffer_size = Read<size_t>();
    if (buffer_size == 0)
    {
        return {};
    }
    if (cur_pointer_ + buffer_size > std::size(buffer_))
    {
        throw ReaderException();
    }

    const std::string_view sv{std::data(buffer_) + cur_pointer_, buffer_size};
    cur_pointer_ += buffer_size;

    return sv;
}

void storm::script_cache::BufferWriter::WriteArray(std::string_view data)
{
    buffer_.reserve(std::size(buffer_) + sizeof(decltype(std::size(data))) + std::size(data));
    WriteData(std::size(data));
    std::ranges::copy(data, std::back_inserter(buffer_));
}

std::string_view storm::script_cache::BufferWriter::GetDataBuffer() const noexcept
{
    return {std::data(buffer_), std::size(buffer_)};
}

namespace
{
// attribute tree of a global object initialised by an object literal: value, children (name, tree)
void WriteAttributes(storm::script_cache::BufferWriter &writer, ATTRIBUTES *attributes)
{
    writer.WriteArray(attributes ? to_string(attributes->GetThisAttr()) : std::string());
    const size_t count = attributes ? attributes->GetAttributesNum() : 0;
    writer.WriteData(count);
    for (size_t n = 0; n < count; n++)
    {
        ATTRIBUTES *child = attributes->GetAttributeClass(static_cast<uint32_t>(n));
        writer.WriteArray(child->GetThisName());
        WriteAttributes(writer, child);
    }
}

void ReadAttributes(storm::script_cache::BufferReader &reader, ATTRIBUTES *attributes)
{
    const auto value = reader.ReadArray();
    if (!value.empty())
        attributes->SetValue(value);
    const auto count = reader.Read<size_t>();
    for (size_t n = 0; n < count; n++)
    {
        const std::string name(reader.ReadArray());
        ReadAttributes(reader, &attributes->CreateAttribute(name));
    }
}
} // namespace

void storm::script_cache::ReadScriptData(BufferReader &reader, S_TOKEN_TYPE type, DATA *data)
{
    switch (type)
    {
    case VAR_OBJECT:
        ReadAttributes(reader, data->GetAClass());
        break;
    case VAR_INTEGER: {
        const auto value = reader.Read<int32_t>();
        data->Set(value);
        break;
    }

    case VAR_FLOAT: {
        const auto value = reader.Read<float>();
        data->Set(value);
        break;
    }
    case VAR_STRING: {
        const auto value = reader.ReadArray();
        data->Set(std::string(value));
        break;
    }
    }
}

void storm::script_cache::WriteScriptData(BufferWriter &writer, S_TOKEN_TYPE type, DATA *data)
{
    switch (type)
    {
    case VAR_OBJECT:
        WriteAttributes(writer, data->GetAClass());
        break;
    case VAR_INTEGER: {
        const auto value = data->GetInt();
        writer.WriteData(value);
        break;
    }

    case VAR_FLOAT: {
        const auto value = data->GetFloat();
        writer.WriteData(value);
        break;
    }
    case VAR_STRING: {
        const auto value = data->GetString();
        writer.WriteArray(value);
        break;
    }
    }
}
