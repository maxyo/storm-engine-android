#pragma once

#include "message.h"

#include <algorithm>

#include <fmt/format.h>

void MESSAGE::Move2Start()
{
    index = 0;
}

uint8_t MESSAGE::Byte()
{
    ValidateFormat('b');
    return get<uint8_t>(params_[index - 1]);
}

uint16_t MESSAGE::Word()
{
    ValidateFormat('w');
    return get<uint16_t>(params_[index - 1]);
}

int32_t MESSAGE::Long()
{
    ValidateFormat('l');
    return get<int32_t>(params_[index - 1]);
}

int32_t MESSAGE::Dword()
{
    ValidateFormat('u');
    return static_cast<int32_t>(get<uint32_t>(params_[index - 1]));
}

float MESSAGE::Float()
{
    ValidateFormat('f');
    return get<float>(params_[index - 1]);
}

double MESSAGE::Double()
{
    ValidateFormat('d');
    return get<double>(params_[index - 1]);
}

uintptr_t MESSAGE::Pointer()
{
    ValidateFormat('p');
    return get<uintptr_t>(params_[index - 1]);
}

ATTRIBUTES * MESSAGE::AttributePointer()
{
    ValidateFormat('a');
    return get<ATTRIBUTES *>(params_[index - 1]);
}

entid_t MESSAGE::EntityID()
{
    ValidateFormat('i');
    return get<entid_t>(params_[index - 1]);
}

VDATA * MESSAGE::ScriptVariablePointer()
{
    ValidateFormat('e');
    return get<VDATA *>(params_[index - 1]);
}

CVECTOR MESSAGE::CVector()
{
    ValidateFormat('c');
    return get<CVECTOR>(params_[index - 1]);
}

const std::string & MESSAGE::String()
{
    ValidateFormat('s');
    return get<std::string>(params_[index - 1]);
}

bool MESSAGE::Set(int32_t value)
{
    ValidateFormat('l');
    params_[index - 1] = value;
    return true;
}

bool MESSAGE::Set(uintptr_t value)
{
    ValidateFormat('p');
    params_[index - 1] = value;
    return true;
}

bool MESSAGE::Set(float value)
{
    ValidateFormat('f');
    params_[index - 1] = value;
    return true;
}

bool MESSAGE::Set(std::string value)
{
    ValidateFormat('s');
    params_[index - 1] = std::move(value);
    return true;
}

bool MESSAGE::SetEntity(entid_t value)
{
    ValidateFormat('i');
    params_[index - 1] = value;
    return true;
}

bool MESSAGE::Set(VDATA *value)
{
    ValidateFormat('e');
    params_[index - 1] = value;
    return true;
}

bool MESSAGE::Set(ATTRIBUTES *value)
{
    ValidateFormat('a');
    params_[index - 1] = value;
    return true;
}

void MESSAGE::ValidateFormat(char c)
{
    const bool exhausted = index < 0 || static_cast<size_t>(index) >= format_.size();
    if (exhausted || format_[index] != c)
    {
        std::string code = "<none>";
        if (!params_.empty() && !format_.empty() && format_.front() == 'l')
            if (const auto *value = std::get_if<int32_t>(&params_.front())) code = std::to_string(*value);
        const auto actual = exhausted ? "<end>" : fmt::format("'{}'", format_[index]);
        throw MessageFormatError(fmt::format("{}: expected='{}' actual={} parameter={} format='{}' messageCode={}",
            format_.empty() ? "Read from empty message" : "Incorrect message data", c, actual, index, format_, code));
    }
    index++;
}

void MESSAGE::Reset(const std::string_view &format)
{
    format_ = format;
    params_.resize(format_.size());
    index = 0;
}

void MESSAGE::ResetVA(const std::string_view &format, va_list&args)
{
    index = 0;
    format_ = format;
    params_.resize(format_.size());
    std::transform(format_.begin(), format_.end(), params_.begin(),
                   [&](const char c) { return GetParamValue(c, args); });
}

char MESSAGE::GetCurrentFormatType()
{
    return format_[index];
}

const char * MESSAGE::StringPointer()
{
    return String().c_str();
}

std::string_view MESSAGE::GetFormat() const
{
    return format_;
}

bool MESSAGE::ParamValid() const
{
    return index < format_.length();
}

storm::MessageParam MESSAGE::GetParamValue(const char c, va_list&args)
{
    switch (c)
    {
    case 'b':
        return va_arg(args, uint8_t);
    case 'w':
        return va_arg(args, uint16_t);
    case 'l':
        return va_arg(args, int32_t);
    case 'u':
        return va_arg(args, uint32_t);
    case 'f':
        return static_cast<float>(va_arg(args, double));
    case 'd':
        return va_arg(args, double);
    case 'p':
        return va_arg(args, uintptr_t);
    case 'a':
        return va_arg(args, ATTRIBUTES *);
    case 'i':
        return va_arg(args, entid_t);
    case 'e':
        return va_arg(args, VDATA *);
    case 'c':
        return va_arg(args, CVECTOR);
    case 's': {
        char *ptr = va_arg(args, char *);
        return std::string(ptr);
    }
    default:
        throw std::runtime_error(fmt::format("Unknown message format: '{}'", c));
    }
}
