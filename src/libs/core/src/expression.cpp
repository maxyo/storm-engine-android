#include "compiler.h"

char ERR_INVALID_EXPRESSION[] = "Invalid Expression";

bool COMPILER::BC_ProcessExpression(DATA *value)
{
    DATA *pV;
    DATA value2;

    value2.SetVCompiler(this);
    value->SetVCompiler(this);
    value->ClearType();

    auto check_equal = false;

    while (BC_TokenGet() == DEBUG_LINE_CODE)
    {
    }
    if (TokenType() == SEPARATOR)
    {
        SetError("Invalid expression");
        return false;
    }

    //--------------------------------------------------------------------------
    if (TokenIs(AND))
    {
        const VarInfo *real_var;

        auto Token_type = BC_TokenGet();
        if (TokenType() != VARIABLE)
        {
            if (TokenType() != LOCAL_VARIABLE)
            {
                SetError("invalid '&'");
                return false;
            }
        }

        const auto var_code = *((uint32_t *)&pRunCodeBase[TLR_DataOffset]);
        if (TokenType() == VARIABLE)
        {
            real_var = VarTab.GetVar(var_code);
            if (real_var == nullptr)
            {
                SetError("Global variable not found");
                return false;
            }
            pV = real_var->value.get();
        }
        else
        {
            // local variable
            if (pRun_fi)
            {
                pV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            else
                return false;
        }

        Token_type = BC_TokenGet();
        if (Token_type == SQUARE_OPEN_BRACKET)
        {
            DATA array_index;
            int32_t index;
            array_index.SetVCompiler(this);

            if (!pV->IsArray())
            {
                SetError("invalid '[' operator");
                return false;
            }
            BC_TokenGet(); // beginning of subexpression
            BC_ProcessExpression_L2(&array_index);
            array_index.Get(index);
            if (!TokenIs(SQUARE_CLOSE_BRACKET))
                SetError("missed ']' in expression");

            value->SetType(VAR_REFERENCE);
            // pV = pV->GetVarPointer();
            pV = pV->GetArrayElement(index);
            value->SetReference(pV); //->GetVarPointer());
            // BC_TokenGet();
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            return true;
        }
        value->SetType(VAR_REFERENCE);
        value->SetReference(pV); //->GetVarPointer());
        return true;
    }

    BC_ProcessExpression_L0(value);
    //--------------------------------------------------------------------------
    /*    BC_ProcessExpression_L2(value);
      S_TOKEN_TYPE op;
      op = TokenType();
      switch(op)
      {
        case SQUARE_CLOSE_BRACKET: break;
        case CLOSE_BRACKET: break;
        case OP_EQUAL: break;

        case OP_BOOL_AND:
        case OP_BOOL_OR:
        case OP_NOT_EQUAL:
        case OP_LESSER_OR_EQUAL:
        case OP_LESSER:
        case OP_GREATER_OR_EQUAL:
        case OP_GREATER:
        case OP_BOOL_EQUAL:
          //BC_TokenGet();
          while(BC_TokenGet() == DEBUG_LINE_CODE){};
          BC_ProcessExpression_L2(&value2);
          value->CompareAndSetResult(&value2,op);
        break;

        case COMMA: break;
        case DOT: break;
        case ACCESS_WORD: break;
        case ACCESS_VAR: break;
        case DEBUG_LINE_CODE: break;

        case SEPARATOR: break;//return value;
        default:
          SetError("Invalid expression break");
        return false;
      }//*/
    return true;
}

// logical
void COMPILER::BC_ProcessExpression_L0(DATA *value)
{
    int32_t lRes;
    DATA value2;
    value2.SetVCompiler(this);

    BC_ProcessExpression_L1(value);
    while (TokenIs(OP_BOOL_AND) || TokenIs(OP_BOOL_OR))
    {
        const auto op = TokenType();
        switch (op)
        {
        case SQUARE_CLOSE_BRACKET:
            break;
        case CLOSE_BRACKET:
            break;
        case OP_EQUAL:
            break;
        case COMMA:
            break;
        case DOT:
            break;
        case ACCESS_WORD:
            break;
        case ACCESS_WORD_CODE:
            break;
        case ACCESS_VAR:
            break;
        case DEBUG_LINE_CODE:
            break;
        case SEPARATOR:
            break;

        case OP_BOOL_AND:
            value->BoolConvert();
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            value->Get(lRes);
            if (lRes == 0)
                BC_ProcessExpression_L1(&value2, true); // SkipExpression_L1();
            else
            {
                BC_ProcessExpression_L1(&value2);
                value->RefConvert();
                value->CompareAndSetResult(&value2, op);
            }
            break;
        case OP_BOOL_OR:
            value->BoolConvert();
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            value->Get(lRes);
            if (lRes)
                BC_ProcessExpression_L1(&value2, true); // SkipExpression_L1();
            else
            {
                BC_ProcessExpression_L1(&value2);
                value->RefConvert();
                value->CompareAndSetResult(&value2, op);
            }
            break;

        default:
            SetError("Invalid expression break");
            return;
        }
    }
}

// logical
void COMPILER::BC_ProcessExpression_L1(DATA *value, bool bSkip)
{
    DATA value2;
    value2.SetVCompiler(this);

    BC_ProcessExpression_L2(value, bSkip);
    while (TokenIs(OP_NOT_EQUAL) || TokenIs(OP_LESSER_OR_EQUAL) || TokenIs(OP_LESSER) || TokenIs(OP_GREATER_OR_EQUAL) ||
           TokenIs(OP_GREATER) || TokenIs(OP_BOOL_EQUAL))
    {
        const auto op = TokenType();
        switch (op)
        {
        case SQUARE_CLOSE_BRACKET:
            break;
        case CLOSE_BRACKET:
            break;
        case OP_EQUAL:
            break;
        case COMMA:
            break;
        case DOT:
            break;
        case ACCESS_WORD:
            break;
        case ACCESS_WORD_CODE:
            break;
        case ACCESS_VAR:
            break;
        case DEBUG_LINE_CODE:
            break;
        case SEPARATOR:
            break;
        case OP_NOT_EQUAL:
        case OP_LESSER_OR_EQUAL:
        case OP_LESSER:
        case OP_GREATER_OR_EQUAL:
        case OP_GREATER:
        case OP_BOOL_EQUAL:
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            BC_ProcessExpression_L2(&value2, bSkip);
            if (!bSkip)
            {
                value->RefConvert();
                value->CompareAndSetResult(&value2, op);
            }
            break;
        default:
            SetError("Invalid expression break");
            return;
        }
    }
}

// '+' and '-' operations
void COMPILER::BC_ProcessExpression_L2(DATA *value, bool bSkip)
{
    DATA TempVal;
    TempVal.SetVCompiler(this);
    BC_ProcessExpression_L3(value, bSkip);
    while (TokenIs(OP_PLUS) || TokenIs(OP_MINUS))
    {
        const auto op = TokenType();
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        TempVal.ClearType();
        BC_ProcessExpression_L3(&TempVal, bSkip);
        if (!bSkip)
        {
            switch (op)
            {
            case OP_PLUS:
                value->Plus(&TempVal);
                break;
            case OP_MINUS:
                value->Minus(&TempVal);
                break;
            }
        }
    }
}

// '*' and '/' operations
void COMPILER::BC_ProcessExpression_L3(DATA *value, bool bSkip)
{
    DATA TempVal;
    TempVal.SetVCompiler(this);

    BC_ProcessExpression_L4(value, bSkip);
    while (TokenIs(OP_MULTIPLY) || TokenIs(OP_DIVIDE) || TokenIs(OP_MODUL))
    {
        const S_TOKEN_TYPE op = TokenType();
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        BC_ProcessExpression_L4(&TempVal, bSkip);
        if (!bSkip)
        {
            switch (op)
            {
            case OP_MULTIPLY:
                value->Multiply(&TempVal);
                break;
            case OP_DIVIDE:
                value->Divide(&TempVal);
                break;
            case OP_MODUL:
                value->Modul(&TempVal);
                break;
            }
        }
    }
}

// '^' operations
void COMPILER::BC_ProcessExpression_L4(DATA *value, bool bSkip)
{
    DATA TempVal;
    TempVal.SetVCompiler(this);
    int32_t Deg;

    BC_ProcessExpression_L5(value, bSkip);
    if (TokenIs(OP_POWER))
    {
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        BC_ProcessExpression_L5(&TempVal, bSkip);
        if (!bSkip)
        {
            if (TempVal.GetType() == VAR_INTEGER)
            {
                TempVal.Get(Deg);
                value->Power(Deg);
            }
        }
    }
}

// sign
void COMPILER::BC_ProcessExpression_L5(DATA *value, bool bSkip)
{
    const S_TOKEN_TYPE op = TokenType();
    if (TokenIs(OP_PLUS) || TokenIs(OP_MINUS) || TokenIs(OP_BOOL_NEG))
    {
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
    }
    BC_ProcessExpression_L6(value, bSkip);

    if (!bSkip)
    {
        if (op == OP_MINUS)
            value->Inverse();
        else if (op == OP_BOOL_NEG)
        {
            int32_t iVal;
            const char *sVal;

            switch (value->GetType())
            {
            case VAR_FLOAT:
                value->Convert(VAR_INTEGER);
            case VAR_INTEGER:
                value->Get(iVal);
                if (iVal != 0)
                    iVal = 0;
                else
                    iVal = 1;
                value->Set(iVal);
                break;
            case VAR_STRING:
                value->Get(sVal);
                if (sVal == nullptr)
                    iVal = 1;
                else if (sVal[0] == 0)
                    iVal = 1;
                else
                    iVal = 0;
                value->Convert(VAR_INTEGER);
                value->Set(iVal);
                break;
            default:
                SetError("Invalid argument type for '!' opearator");
                break;
            }
        }
    }
}

// '(' and ')'
void COMPILER::BC_ProcessExpression_L6(DATA *value, bool bSkip)
{
    if (TokenIs(OPEN_BRACKET))
    {
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        BC_ProcessExpression_L1(value, bSkip);
        if (!TokenIs(CLOSE_BRACKET))
            SetError("No matching ')' in expression");
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
    }
    else
        BC_ProcessExpression_L7(value, bSkip);
}

void COMPILER::BC_ProcessExpression_L7(DATA *value, bool bSkip)
{
    const VarInfo *real_var;
    uint32_t var_code;
    int32_t index;
    DATA array_index;
    DATA TempData;
    TempData.SetVCompiler(this);
    array_index.SetVCompiler(this);
    DATA *pV;
    DATA *pVV;
    DATA Access;
    Access.SetVCompiler(this);
    DATA *pFuncResult;
    ATTRIBUTES *pRoot;
    const char *pString;
    uint32_t func_code, ip;
    S_TOKEN_TYPE vtype;

    switch (TokenType())
    {
    case NUMBER:
        if (bSkip)
        {
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            return;
        }
        value->Set(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        return;
    case FLOAT_NUMBER:
        if (bSkip)
        {
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            return;
        }
        value->Set(*((float *)&pRunCodeBase[TLR_DataOffset]));
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        break;
    case STRING:
        if (bSkip)
        {
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            return;
        }
        value->Set((char *)&pRunCodeBase[TLR_DataOffset + 4]); // 4 - string length
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        break;
    case CALL:
        if (bSkip)
        {
            int32_t local_in;
            BC_TokenGet(); // string var
            if (BC_TokenGet() != OPEN_BRACKET)
            {
                SetError("missing '('");
                return;
            }
            local_in = 1;
            do
            {
                switch (BC_TokenGet())
                {
                case OPEN_BRACKET:
                    local_in++;
                    break;
                case CLOSE_BRACKET:
                    local_in--;
                    break;
                }
            } while (local_in > 0);
            BC_TokenGet();
            return;
        }
        vtype = BC_TokenGet();                               // read variable
        var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]); // var code
        if (vtype == VARIABLE)
        {
            real_var = VarTab.GetVar(var_code);
            if (real_var == nullptr)
            {
                SetError("Global variable not found");
                return;
            }

            pVV = real_var->value.get();
        }
        else
        {
            pVV = SStack.Read(pRun_fi->stack_offset, var_code);
            if (pVV == nullptr)
            {
                SetError("Local variable not found");
                return;
            }
        }
        pVV = pVV->GetVarPointer();
        if (pVV->GetType() != VAR_STRING)
        {
            SetError("string call argument isnt string var");
            return;
        }
        pVV->Get(pString); // func name
        func_code = FuncTab.FindFunc(pString);
        if (func_code == INVALID_FUNC_CODE)
        {
            SetError("function '%s' not found", pString);
            return;
        }

        pFuncResult = nullptr;
        if (!BC_CallFunction(func_code, ip, pFuncResult))
            return;
        if (pFuncResult)
        {
            SStack.Pop();
            value->Copy(pFuncResult);
        }
        else
            SetError("void function result in expression");
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }

        break;
    case CALL_FUNCTION:
        if (bSkip)
        {
            int32_t local_in;
            if (BC_TokenGet() != OPEN_BRACKET)
            {
                SetError("missing '('");
                return;
            }
            local_in = 1;
            do
            {
                switch (BC_TokenGet())
                {
                case OPEN_BRACKET:
                    local_in++;
                    break;
                case CLOSE_BRACKET:
                    local_in--;
                    break;
                }
            } while (local_in > 0);
            BC_TokenGet();
            // while(BC_TokenGet() == DEBUG_LINE_CODE){};
            return;
        }
        // DWORD func_code,ip;
        memcpy(&func_code, &pRunCodeBase[TLR_DataOffset], sizeof(uint32_t));
        pFuncResult = nullptr;
        if (!BC_CallFunction(func_code, ip, pFuncResult))
            return;
        if (pFuncResult)
        {
            SStack.Pop();
            value->Copy(pFuncResult);
        }
        else
            SetError("void function result in expression");
        while (BC_TokenGet() == DEBUG_LINE_CODE)
        {
        }
        break;
    case LOCAL_VARIABLE:
    case VARIABLE:
        var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
        if (TokenType() == VARIABLE)
        {
            real_var = VarTab.GetVar(var_code);
            if (real_var == nullptr)
            {
                SetError("Global variable not found");
                break;
            }

            pV = real_var->value.get();
        }
        else
        {
            // local variable
            if (pRun_fi)
            {
                pV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    break;
                }
            }
            else
                return;
        }

        // *****

        bool bref2a;
        bref2a = false;
        if (pV->IsReference())
        {
            pVV = pV->GetVarPointer();
            if (pVV == nullptr)
            {
                SetError("Bad Reference");
                break;
            }
            if (pVV->IsArray())
                bref2a = true;
        }

        if (BC_TokenGet() == SQUARE_OPEN_BRACKET)
        {
            if (!(pV->IsArray() || bref2a))
            {
                SetError("wrong '[' in expression");
                return;
            }
            // beginning of subexpression
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
            BC_ProcessExpression_L2(&array_index, bSkip);
            array_index.Get(index);
            if (!TokenIs(SQUARE_CLOSE_BRACKET))
                SetError("missed ']' in expression");
            if (!bSkip)
            {
                if (bref2a)
                    pV = pVV->GetArrayElement(index);
                else
                    pV = pV->GetArrayElement(index);
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    while (BC_TokenGet() == DEBUG_LINE_CODE)
                    {
                    }
                    break;
                }
            }
            BC_TokenGet();
        }

        //-----

        /*
        if(pV->IsArray() || bref2a)
        {
          // compute array index
          if(BC_TokenGet() != SQUARE_OPEN_BRACKET) { SetError("missed '[' in expression"); return; }
          // beginning of subexpression
          while(BC_TokenGet() == DEBUG_LINE_CODE){};
          BC_ProcessExpression_L2(&array_index,bSkip);
          array_index.Get(index);
          if(!TokenIs(SQUARE_CLOSE_BRACKET)) SetError("missed ']' in expression");
          if(!bSkip)
          {
            if(bref2a) pV = pVV->GetArrayElement(index);
            else pV = pV->GetArrayElement(index);
            if(pV == 0)
            {
              SetError("invalid array index");
              while(BC_TokenGet() == DEBUG_LINE_CODE){};
              break;
            }
          }
        }//*/

        while (TokenType() == DEBUG_LINE_CODE)
        {
            BC_TokenGet();
        }
        // while(BC_TokenGet() == DEBUG_LINE_CODE){};
        if (TokenType() != ACCESS_WORD_CODE && TokenType() != ACCESS_WORD && TokenType() != ACCESS_VAR)
        {
            // pV = pV->GetVarPointer();
            // if(!pV) {SetError("invalid ref in process expression"); return;}
            if (!bSkip)
            {
                if (!pV->IsArray())
                {
                    value->Copy(pV);
                    return;
                }                       // copy single value
                value->Copy(pV, index); // copy array element value
            }
            return;
        }

        if (bSkip)
        {
            while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
            {
                BC_TokenGet();
                while (TokenType() == DEBUG_LINE_CODE)
                {
                    BC_TokenGet();
                }
            }
            return;
        }
        // accessing to atribute (string) value
        pRoot = pV->GetAClass(); // root attribute class
        if (pRoot == nullptr)
        {
            SetError("zero AClass root");
            value->Set("ERROR: zero AClass root");
            while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
            {
                BC_TokenGet();
            }
            return;
            // throw std::runtime_error("zero AClass root");
        }
        while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
        {
            if (TokenType() == ACCESS_WORD_CODE)
            {
                if (pRoot == nullptr)
                {
                    value->Set("");
                    SetError("missed attribute: %s", SCodec.Convert(*((int32_t *)&pRunCodeBase[TLR_DataOffset])));
                    while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
                    {
                        BC_TokenGet();
                    }
                    return;
                }
                pRoot = pRoot->GetAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                if (pRoot == nullptr)
                {
                    value->Set("");
                    SetError("missed attribute: %s", SCodec.Convert(*((int32_t *)&pRunCodeBase[TLR_DataOffset])));
                    while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
                    {
                        BC_TokenGet();
                    }
                    return;
                }
            }
            else if (TokenType() == ACCESS_WORD)
            {
                if (pRoot == nullptr)
                {
                    value->Set("");
                    SetError("missed attribute: %s", (char *)&pRunCodeBase[TLR_DataOffset]);
                    while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
                    {
                        BC_TokenGet();
                    }
                    return;
                }
                pRoot = pRoot->GetAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
                if (pRoot == nullptr)
                {
                    value->Set("");
                    SetError("missed attribute: %s", (char *)&pRunCodeBase[TLR_DataOffset]);
                    while (TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
                    {
                        BC_TokenGet();
                    }
                    return;
                }
            }
            else
            {
                // BC_TokenGet();
                while (BC_TokenGet() == DEBUG_LINE_CODE)
                {
                }
                var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
                if (TokenType() == VARIABLE)
                {
                    real_var = VarTab.GetVar(var_code);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        break;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    // local variable
                    if (pRun_fi)
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, var_code);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            break;
                        }
                    }
                    else
                        return;
                }

                TempData.Copy(pV);
                TempData.Convert(VAR_STRING);
                TempData.Get(pString);

                pRoot = pRoot->FindAClass(pRoot, pString);
                if (pRoot == nullptr)
                {
                    value->Set("");
                    SetError("missed attribute %s", pString);
                    while (BC_TokenGet() == DEBUG_LINE_CODE)
                    {
                    }
                    return;
                }
                // pRoot = pRoot->GetAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
            }
            // BC_TokenGet();
            while (BC_TokenGet() == DEBUG_LINE_CODE)
            {
            }
        }
        pString = pRoot->GetThisAttr();
        value->Set(pString);
        break;
    }
}

//===============================================================================================================

// test for makeref
bool COMPILER::CompileExpression(SEGMENT_DESC &Segment)
{
    uint32_t dwVarCode;
    const VarInfo *real_var;
    LocalVarInfo lvi;

    const S_TOKEN_TYPE Token_type = CompileAuxiliaryTokens(Segment);
    if (Token_type == SEPARATOR)
        return false; // the failing operand reported the error

    if (Token_type == AND)
    {
        // make reference operation

        S_TOKEN_TYPE sttVariableType = Token.Get();

        if (sttVariableType != UNKNOWN)
        {
            SetError("invalid '&' usage");
            return false;
        }

        sttVariableType = DetectUnknown(dwVarCode);
        if (sttVariableType == CALL_FUNCTION)
        {
            // &Function: a function reference
            const std::string name = Token.GetData();
            const uint32_t size = static_cast<uint32_t>(name.size() + 1);
            CompileToken(Segment, STACK_PUSH);
            CompileToken(Segment, STRING, 2, &size, sizeof(uint32_t), name.c_str(), size);
            uint32_t target = VAR_FREF;
            CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (!(sttVariableType == VARIABLE || sttVariableType == LOCAL_VARIABLE))
        {
            SetError("variable not found");
            return false;
        }

        const S_TOKEN_TYPE sttTokenType = Token.Get();
        if (sttTokenType == SQUARE_OPEN_BRACKET && IsArefVariable(sttVariableType, dwVarCode))
        {
            // &aref[i]: its i-th attribute
            if (!CompileExpression(Segment))
                return false;
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, MOVEAP);
            CompileToken(Segment, AP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));
            CompileToken(Segment, AP_CHILD);
            CompileToken(Segment, PUSH_AP);
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (sttTokenType == DOT || (sttTokenType == SQUARE_OPEN_BRACKET && IsIndexedChain()))
            return CompileAttributeAddress(Segment, sttVariableType, dwVarCode, kAccessCreate);

        // alloc for result
        CompileToken(Segment, STACK_ALLOC);

        if (sttTokenType == SQUARE_OPEN_BRACKET)
        {
            if (sttVariableType == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwVarCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                // check for possibilities of '[' operator
                if (!real_var->IsArray())
                {
                    if (real_var->type != VAR_REFERENCE)
                    {
                        SetError("EN: %d", real_var->elements);
                        SetError(" A Invalid '[' operator, %s - isnt array", real_var->name.c_str());
                        return false;
                    }
                }
            }
            else
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                // check for possibilities of '[' operator
                if (!lvi.IsArray())
                {
                    if (lvi.type != VAR_REFERENCE)
                    {
                        SetError(" B Invalid '[' operator, %s - isnt array", lvi.name.c_str());
                        return false;
                    }
                }
            }

            // alloc for array index
            // CompileToken(Segment,STACK_ALLOC);
            // after top of the stack must be array index
            Token.Get();
            if (!CompileExpression_L2(Segment))
            {
                SetError("invalid syntax");
                return false;
            }
            // test for ']'
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }

            // copy array index to BX
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);

            CompileToken(Segment, SETREF_BXINDEX);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));

            // next after ']'
            CompileAuxiliaryTokens(Segment);
        }
        else
        {
            CompileToken(Segment, SETREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));
        }

        // proceed to next token
        // CompileAuxiliaryTokens(Segment);

        return true;
    }
    return CompileTernary(Segment);
}

// cond ? a : b
bool COMPILER::CompileTernary(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L0(Segment))
        return false;
    if (Token.GetType() != OP_QUESTION)
        return true;
    uint32_t jump = 0;
    CompileToken(Segment, STACK_POP);
    CompileToken(Segment, EX);
    const uint32_t to_else = Segment.BCode_Program_size + 2;
    CompileToken(Segment, JUMP_Z, 1, &jump, sizeof(uint32_t));
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != LABEL)
    {
        SetError("':' expected in conditional expression");
        return false;
    }
    const uint32_t to_end = Segment.BCode_Program_size + 2;
    CompileToken(Segment, JUMP, 1, &jump, sizeof(uint32_t));
    memcpy(&Segment.pCode[to_else], &Segment.BCode_Program_size, sizeof(uint32_t));
    if (!CompileExpression(Segment))
        return false;
    memcpy(&Segment.pCode[to_end], &Segment.BCode_Program_size, sizeof(uint32_t));
    return true;
}

// binary operators of one level: operand, then (op operand)*; each op is OP STACK_TOP AX
bool COMPILER::CompileBinaryLevel(SEGMENT_DESC &Segment, bool (COMPILER::*operand)(SEGMENT_DESC &),
                                  std::initializer_list<S_TOKEN_TYPE> ops, S_TOKEN_TYPE emitted_for_and)
{
    if (!(this->*operand)(Segment))
        return false;
    for (;;)
    {
        S_TOKEN_TYPE op = Token.GetType();
        if (std::find(ops.begin(), ops.end(), op) == ops.end())
            return true;
        CompileAuxiliaryTokens(Segment);
        if (!(this->*operand)(Segment))
            return false;
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, op == AND ? emitted_for_and : op);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
}

bool COMPILER::CompileBitOr(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileBitAnd, {OP_BIT_OR}, OP_BIT_AND);
}

bool COMPILER::CompileBitAnd(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileExpression_L1, {AND}, OP_BIT_AND);
}

bool COMPILER::CompileShift(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileExpression_L2, {OP_SHL, OP_SHR}, OP_BIT_AND);
}

// logical operators (OR,AND)
bool COMPILER::CompileExpression_L0(SEGMENT_DESC &Segment)
{
    uint32_t dwJumpOffset1;
    uint32_t dwJumpOffset2;

    if (!CompileBitOr(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_BOOL_AND || sttOpType == OP_BOOL_OR)
    {
        sttOpType = Token.GetType();

        CompileAuxiliaryTokens(Segment);

        // copy to EX value of first operand
        /*CompileToken(Segment,MOVE);
        CompileToken(Segment,EX);
        CompileToken(Segment,STACK_TOP);*/

        CompileToken(Segment, STACK_READ);
        CompileToken(Segment, EX);

        // save jump update position
        const uint32_t dwUpdateOffset1 = Segment.BCode_Program_size + 2;
        // point of jump not determined yet
        dwJumpOffset1 = 0;
        // compile jump command
        if (sttOpType == OP_BOOL_AND)
        {
            // AND : if 1st operand is zero - doesnt need to calculate second - jump to jump1
            CompileToken(Segment, JUMP_Z, 1, (char *)&dwJumpOffset1, sizeof(uint32_t));
        }
        else
        {
            // OR : if 1st operand is non zero - doesnt need to calculate second - jump to jump1
            CompileToken(Segment, JUMP_NZ, 1, (char *)&dwJumpOffset1, sizeof(uint32_t));
        }

        if (!CompileBitOr(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);

        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        // skip next section - proceed to jump2
        const uint32_t dwUpdateOffset2 = Segment.BCode_Program_size + 2;
        dwJumpOffset2 = 0;
        CompileToken(Segment, JUMP, 1, (char *)&dwJumpOffset2, sizeof(uint32_t));

        // jump1
        // just convert stack top to bool
        memcpy(&Segment.pCode[dwUpdateOffset1], &Segment.BCode_Program_size, sizeof(uint32_t));
        CompileToken(Segment, OP_BOOL_CONVERT);
        CompileToken(Segment, STACK_TOP);

        // jump2
        // end of operation
        memcpy(&Segment.pCode[dwUpdateOffset2], &Segment.BCode_Program_size, sizeof(uint32_t));

        sttOpType = Token.GetType();
    }
    return true;
}

// logical compare (>,<,==,>=,<=,!=)
bool COMPILER::CompileExpression_L1(SEGMENT_DESC &Segment)
{
    if (!CompileShift(Segment))
        return false; // the failing operand reported the error

    for (;;)
    {
        S_TOKEN_TYPE sttOpType = Token.GetType();
        const char *word = sttOpType == UNKNOWN && Token.GetData() ? Token.GetData() : "";
        if (sttOpType == OP_BOOL_NEG || storm::iEquals(word, "in") || storm::iEquals(word, "is"))
        {
            // "key" in object, "key" !in object, x is nullptr
            bool negate = sttOpType == OP_BOOL_NEG;
            if (negate)
            {
                Token.Get();
                if (!(Token.GetType() == UNKNOWN && storm::iEquals(Token.GetData(), "in")))
                {
                    SetError("'in' expected after '!'");
                    return false;
                }
                word = "in";
            }
            if (storm::iEquals(word, "is"))
            {
                Token.Get();
                if (!(Token.GetType() == UNKNOWN && storm::iEquals(Token.GetData(), "nullptr")))
                {
                    SetError("'nullptr' expected after 'is'");
                    return false;
                }
                CompileToken(Segment, OP_IS_NULL);
                CompileToken(Segment, STACK_TOP);
                CompileAuxiliaryTokens(Segment);
                continue;
            }
            CompileAuxiliaryTokens(Segment);
            if (!CompileAttributeRoot(Segment))
                return false;
            CompileToken(Segment, OP_IN);
            if (negate)
            {
                CompileToken(Segment, OP_BOOL_NEG);
                CompileToken(Segment, STACK_TOP);
            }
            continue;
        }
        if (!(sttOpType == OP_NOT_EQUAL || sttOpType == OP_LESSER_OR_EQUAL || sttOpType == OP_LESSER ||
              sttOpType == OP_GREATER_OR_EQUAL || sttOpType == OP_GREATER || sttOpType == OP_BOOL_EQUAL))
            return true;

        CompileAuxiliaryTokens(Segment);
        if ((sttOpType == OP_BOOL_EQUAL || sttOpType == OP_NOT_EQUAL) && Token.GetType() == UNKNOWN &&
            storm::iEquals(Token.GetData(), "nullptr"))
        {
            // x == nullptr: a null reference, attribute reference or function reference
            CompileToken(Segment, OP_IS_NULL);
            CompileToken(Segment, STACK_TOP);
            if (sttOpType == OP_NOT_EQUAL)
            {
                CompileToken(Segment, OP_BOOL_NEG);
                CompileToken(Segment, STACK_TOP);
            }
            CompileAuxiliaryTokens(Segment);
            continue;
        }

        if (!CompileShift(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
}

bool COMPILER::CompileExpression_L2(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L3(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_PLUS || sttOpType == OP_MINUS)
    {
        sttOpType = Token.GetType();

        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L3(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        sttOpType = Token.GetType();
    }
    return true;
}

// '*' and '/' operators
bool COMPILER::CompileExpression_L3(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L4(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_MULTIPLY || sttOpType == OP_DIVIDE || sttOpType == OP_MODUL)
    {
        sttOpType = Token.GetType();
        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L4(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        sttOpType = Token.GetType();
    }
    return true;
}

// '^' operations
bool COMPILER::CompileExpression_L4(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L5(Segment))
        return false; // the failing operand reported the error

    if (Token.GetType() == OP_POWER)
    {
        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L5(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, OP_POWER);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
    return true;
}

// sign
bool COMPILER::CompileExpression_L5(SEGMENT_DESC &Segment)
{
    const S_TOKEN_TYPE sttOpType = Token.GetType();
    if (sttOpType == OP_DOLLAR)
    {
        // $(x): the text of x
        if (Token.Get() != OPEN_BRACKET)
        {
            SetError("'(' expected after '$'");
            return false;
        }
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != CLOSE_BRACKET)
        {
            SetError("')' expected after $(...");
            return false;
        }
        uint32_t target = VAR_STRING;
        CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    }
    if (sttOpType == OP_SIZEOF)
    {
        // @array: number of elements; @object / @aref: number of attributes
        CompileAuxiliaryTokens(Segment);
        if (!CompileAttributeRoot(Segment))
            return false;
        CompileToken(Segment, OP_SIZEOF_REF);
        CompileToken(Segment, STACK_TOP);
        return true;
    }
    if (sttOpType == OP_PLUS || sttOpType == OP_MINUS || sttOpType == OP_BOOL_NEG || sttOpType == OP_BIT_NOT)
        CompileAuxiliaryTokens(Segment);

    if (!CompileExpression_L6(Segment))
        return false; // the failing operand reported the error

    switch (sttOpType)
    {
    case OP_MINUS:
        CompileToken(Segment, OP_SMINUS);
        CompileToken(Segment, STACK_TOP);
        break;
    case OP_BOOL_NEG:
        CompileToken(Segment, OP_BOOL_NEG);
        CompileToken(Segment, STACK_TOP);
        break;
    case OP_BIT_NOT:
        CompileToken(Segment, OP_BIT_NOT);
        CompileToken(Segment, STACK_TOP);
        break;
    }
    return true;
}

// '(' and ')'
bool COMPILER::CompileExpression_L6(SEGMENT_DESC &Segment)
{
    if (Token.GetType() == OPEN_BRACKET)
    {
        CompileAuxiliaryTokens(Segment);
        if (!CompileTernary(Segment))
            return false;
        if (Token.GetType() != CLOSE_BRACKET)
        {
            SetError("No matching ')' in expression");
            return false;
        }
        if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET)
            return CompileSlice(Segment, STACK_TOP, 0); // (expression)[a .. b]
        return true;
    }
    return CompileExpression_L7(Segment);
}

// read value
bool COMPILER::CompileExpression_L7(SEGMENT_DESC &Segment)
{
    uint32_t dwRCode = 0;
    S_TOKEN_TYPE sttResult;
    S_TOKEN_TYPE sttVariableField;
    S_TOKEN_TYPE sttFuncCallType;
    uint32_t dwVarCode;
    uint32_t dwAWCode;
    const VarInfo *real_var;
    LocalVarInfo lvi;
    FuncInfo fi;

    bool bDynamicCall = false;
    std::vector<uint32_t> chain; // offsets of the ADVANCE_AP instructions of an attribute chain
    switch (Token.GetType())
    {
    case SEPARATOR:

        return true;
    case VAR_INTEGER:
    case VAR_FLOAT:
    case VAR_STRING:
    case VAR_FREF:
    case VAR_AREFERENCE:
        if (Token.TypeCast())
            return CompileCast(Segment);
        break;
    case BLOCK_IN:
        return CompileObjectLiteral(Segment);
    case NUMBER:
        CompileToken(Segment, STACK_PUSH);
        CompileNumber(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case FLOAT_NUMBER:
        CompileToken(Segment, STACK_PUSH);
        CompileFloatNumber(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case STRING:
        CompileToken(Segment, STACK_PUSH);
        CompileString(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case TRUE_CONST:
        CompileToken(Segment, STACK_PUSH);
        dwRCode = 1;
        CompileToken(Segment, NUMBER, 1, (char *)&dwRCode, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    case FALSE_CONST:
        CompileToken(Segment, STACK_PUSH);
        dwRCode = 0;
        CompileToken(Segment, NUMBER, 1, (char *)&dwRCode, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;

    case CALL:
        bDynamicCall = true;
    case UNKNOWN:
        if (!bDynamicCall && Token.GetData() && storm::iEquals(Token.GetData(), "nullptr"))
        {
            CompileToken(Segment, PUSH_NULL);
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (!bDynamicCall && Token.GetData() && storm::iEquals(Token.GetData(), "typeid"))
            return CompileTypeid(Segment);
        if (!bDynamicCall && Token.GetData() && IsAttributeIntrinsic(Token.GetData()))
            return CompileAttributeIntrinsic(Segment);
        if (!bDynamicCall)
            if (const S_TOKEN_TYPE call_type = DetectCallThrough(); call_type != UNKNOWN)
            {
                if (!CompileCallThrough(Segment, call_type))
                    return false;
                CompileAuxiliaryTokens(Segment);
                return true;
            }
        if (!bDynamicCall)
        {
            sttResult = DetectUnknown(dwRCode);
            // set func call method to standart
            sttFuncCallType = CALL_FUNCTION;
        }
        else
        {
            sttResult = CALL;
            sttFuncCallType = CALL_FUNCTION;
        }

        switch (sttResult)
        {
        case DEFINE_VAL:
            DEFINFO di;
            DefTab.GetDef(di, dwRCode);
            switch (di.deftype)
            {
            case NUMBER:
                CompileToken(Segment, STACK_PUSH);
                CompileToken(Segment, NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
                CompileAuxiliaryTokens(Segment);
                break;
            case FLOAT_NUMBER:
                CompileToken(Segment, STACK_PUSH);
                CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
                CompileAuxiliaryTokens(Segment);
                break;
            case STRING:
                CompileToken(Segment, STACK_PUSH);
                uint32_t string_size;
                string_size = strlen((char *)di.data4b) + 1;
                CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), (char *)di.data4b,
                             string_size);
                CompileAuxiliaryTokens(Segment);
                break;
            }
            return true;
        case CALL:
            // this is dynamic function call

            // call type - dynamic
            sttFuncCallType = CALL;

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid dynamic call syntax");
                return false;
            }

            // check for string variable
            sttVariableField = DetectUnknown(dwVarCode);

            if (sttVariableField == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwVarCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                if (real_var->type != VAR_STRING)
                {
                    SetError("'%s' must be string variable", Token.GetData());
                    return false;
                }
            }
            else if (sttVariableField == LOCAL_VARIABLE)
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                if (lvi.type != VAR_STRING)
                {
                    SetError("'%s' must be string variable", Token.GetData());
                    return false;
                }
            }
            else
            {
                SetError("invalid dynamic call syntax: '%s' must be string variable", Token.GetData());
                return false;
            }

            // proceed to standart func call routine

        case CALL_FUNCTION:
            // this is function call

            // first prepare stack for calling function

            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missing '('");
                return false;
            }

            // just get, already checked by DetectUnknown
            FuncTab.GetFuncX(fi, dwRCode);

            // if(fi.return_type == TVOID){ SetError("void function in expression: %s",fi.name); return false; }

            uint32_t func_args;
            if (!CompileCallArguments(Segment, func_args))
                return false;
            if (sttFuncCallType == CALL_FUNCTION && !CheckCallArguments(dwRCode, func_args, true))
                return false;

            /*
            // off for debug needs
            if(sttFuncCallType != CALL)    // skip argument checking for dynamic function call
            {
              if(func_args != fi.arguments)
              {
                if(fi.segment_id != INTERNAL_SEGMENT_ID)
                {
                  // not internal function cant accept different arguments number
                  SetError("function %s(args:%d) doesnt accept %d arguments",fi.name,fi.arguments,func_args);
                  return false;
                }
                else
                {
                  if(!IsIntFuncVarArgsNum(dwRCode))
                  {
                    SetError("function '%s(args:%d)' doesnt accept %d arguments",fi.name,fi.arguments,func_args);
                    return false;
                  }

                }
              }
            }//*/

            // call function
            if (sttFuncCallType == CALL_FUNCTION)
                CompileToken(Segment, CALL_FUNCTION, 1, (char *)&dwRCode, sizeof(uint32_t));
            else
            {
                // code of function must be find out on the fly from variable
                CompileToken(Segment, CALL);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
            }
            CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));

            // ...
            // proceed to next token
            CompileAuxiliaryTokens(Segment);

            return true;
        case LOCAL_VARIABLE:
        case VARIABLE:

            // set local or global type
            sttVariableField = sttResult;

            // variable code
            dwVarCode = dwRCode;

            // we going to place into stack variable value or attribute string - alloc stack data
            CompileToken(Segment, STACK_ALLOC);

            sttResult = Token.Get();
            if (sttResult == SQUARE_OPEN_BRACKET && IsSlice())
                return CompileSlice(Segment, sttVariableField, dwVarCode); // text[a .. b]
            if (sttResult == SQUARE_OPEN_BRACKET && IsArefVariable(sttVariableField, dwVarCode))
            {
                // aref[i]: its i-th attribute
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                CompileToken(Segment, AP_CHILD);
                if (Token.Get() == DOT)
                    return CompileChainTail(Segment); // aref[i].name
                CompileToken(Segment, STACK_POP_VOID); // the value slot: the result is the reference
                CompileToken(Segment, PUSH_AP);
                return true;
            }
            if (sttResult == SQUARE_OPEN_BRACKET)
            {
                // way for array element

                // doesnt need to check, becose already pass DetectUnknown function

                if (sttVariableField == VARIABLE)
                {
                    real_var = VarTab.GetVarX(dwVarCode);
                    if (real_var == nullptr)
                    {
                        SetError("Invalid var code");
                        return false;
                    }

                    // check for possibilities of '[' operator
                    if (!real_var->IsArray())
                    {
                        if (real_var->type != VAR_REFERENCE)
                        {
                            SetError(" C Invalid '[' operator, %s - isnt array", real_var->name.c_str());
                            return false;
                        }
                    }
                }
                else
                {
                    FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                    // check for possibilities of '[' operator
                    if (!lvi.IsArray())
                    {
                        if (lvi.type != VAR_REFERENCE)
                        {
                            SetError(" D Invalid '[' operator, %s - isnt array", lvi.name.c_str());
                            return false;
                        }
                    }
                }

                // CompileToken(Segment,STACK_ALLOC);
                // after top of the stack must be array index
                Token.Get();
                if (!CompileExpression_L2(Segment))
                    return false;
                // test for ']'
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                // proceed to next token
                sttResult = CompileAuxiliaryTokens(Segment);

                // if not accessing attribute - copy value and return
                if (sttResult != DOT)
                {
                    // optimiztion possible: MOVE [STACK-1],[STACK]

                    // copy array index to BX
                    CompileToken(Segment, STACK_POP);
                    CompileToken(Segment, BX);

                    // write to stack element value (index in X)
                    CompileToken(Segment, STACK_WRITE_BXINDEX);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));

                    // proceed to next token
                    // CompileAuxiliaryTokens(Segment);

                    return true;
                }

                // in other case want to access attribute string value

                // copy array index to BX
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                // move ap to AP register
                CompileToken(Segment, MOVEAP_BXINDEX);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));

                // proceed to ADVANCE AP
            }
            else
            {
                // way for non array var

                if (sttResult == OP_DOLLAR)
                {
                    CompileToken(Segment, STACK_POP_VOID);
                    CompileToken(Segment, MOVEAP);
                    CompileToken(Segment, AP);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                    return CompileAttributeDefault(Segment);
                }
                if (sttResult != DOT)
                {
                    // copy variavle to stack
                    CompileToken(Segment, STACK_WRITE);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                    return true;
                }

                // in other case want to access attribute string value

                // move ap to AP register
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
            }

            return CompileChainTail(Segment);
        default:
            // ???
            SetError(ERR_INVALID_EXPRESSION);
            return false;
        }
        return true;
    }
    SetError(ERR_INVALID_EXPRESSION);
    return false;
}

// int(x), float(x), bool(x), string(x), kstring(x), fref(x)
bool COMPILER::CompileCast(SEGMENT_DESC &Segment)
{
    const std::string keyword = Token.GetData() ? Token.GetData() : "";
    uint32_t target = Token.GetType();
    if (storm::iEquals(keyword, "bool"))
        target = TRUE_CONST; // marker: truth value
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after %s", keyword.c_str());
        return false;
    }
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after %s(...", keyword.c_str());
        return false;
    }
    CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// typeid(type) is a constant, typeid(expression) the type of its value (through references)
bool COMPILER::CompileTypeid(SEGMENT_DESC &Segment)
{
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after typeid");
        return false;
    }
    const S_TOKEN_TYPE type = Token.Get();
    const char *data = Token.GetData() ? Token.GetData() : "";
    int32_t id = -1;
    switch (type)
    {
    case VAR_INTEGER:
        id = storm::iEquals(data, "bool") ? kTypeidBool : kTypeidInt;
        break;
    case VAR_FLOAT:
        id = kTypeidFloat;
        break;
    case VAR_STRING:
        id = kTypeidString;
        break;
    case VAR_OBJECT:
        id = kTypeidObject;
        break;
    case VAR_REFERENCE:
        id = kTypeidRef;
        break;
    case VAR_AREFERENCE:
        id = kTypeidAref;
        break;
    case VAR_FREF:
        id = kTypeidFref;
        break;
    default:
        break;
    }
    if (id >= 0)
    {
        if (Token.Get() != CLOSE_BRACKET)
        {
            SetError("')' expected after typeid(type");
            return false;
        }
        CompileToken(Segment, STACK_PUSH);
        CompileToken(Segment, NUMBER, 1, &id, sizeof(int32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    }
    Token.StepBack();
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after typeid(...");
        return false;
    }
    CompileToken(Segment, OP_TYPEID);
    CompileToken(Segment, STACK_TOP);
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after the attribute chain (rAP set by MOVEAP/ADVANCE_AP_QUIET), on '$': type(default)
bool COMPILER::CompileAttributeDefault(SEGMENT_DESC &Segment)
{
    CompileToken(Segment, PUSH_AP);
    Token.Get();
    if (Token.GetType() == VAR_AREFERENCE && !Token.TypeCast())
    {
        // a.b$aref: the attribute reference itself (null when missing)
        CompileAuxiliaryTokens(Segment);
        return true;
    }
    const char *keyword = Token.GetData() ? Token.GetData() : "";
    uint32_t target = Token.GetType();
    if (!Token.TypeCast() ||
        !(target == VAR_INTEGER || target == VAR_FLOAT || target == VAR_STRING || target == VAR_FREF))
    {
        SetError("type(default) expected after '$'");
        return false;
    }
    if (storm::iEquals(keyword, "bool"))
        target = TRUE_CONST;
    Token.Get(); // '('
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after the default value");
        return false;
    }
    CompileToken(Segment, ATTR_DEFAULT, 1, &target, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// the operand of in and @: a variable, optionally indexed, and an optional attribute chain. Leaves a
// reference to the variable (no chain) or an attribute reference (null when an attribute is missing).
bool COMPILER::CompileAttributeRoot(SEGMENT_DESC &Segment)
{
    uint32_t var_code;
    if (Token.GetType() == AND)
        Token.Get(); // "key" in &object: the same operand
    if (Token.GetType() != UNKNOWN)
    {
        SetError("variable expected");
        return false;
    }
    const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
    {
        SetError("variable expected, not '%s'", Token.GetData());
        return false;
    }
    bool indexed = false;
    if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET)
    {
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("missing ']'");
            return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, BX);
        indexed = true;
        CompileAuxiliaryTokens(Segment);
    }
    if (Token.GetType() != DOT)
    {
        CompileToken(Segment, STACK_ALLOC);
        CompileToken(Segment, indexed ? SETREF_BXINDEX : SETREF);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        return true;
    }
    CompileToken(Segment, indexed ? MOVEAP_BXINDEX : MOVEAP);
    CompileToken(Segment, AP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    while (Token.GetType() == DOT)
    {
        Token.Get();
        if (Token.GetType() == OPEN_BRACKET)
        {
            if (!CompileAccessExpression(Segment, kAccessQuiet))
                return false;
        }
        else
        {
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            const uint32_t word = SCodec.Convert(Token.GetData());
            CompileToken(Segment, ADVANCE_AP_QUIET);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &word, sizeof(uint32_t));
        }
        CompileAuxiliaryTokens(Segment);
    }
    CompileToken(Segment, PUSH_AP);
    return true;
}

// On an identifier: CALL_METHOD for "object->Method(", CALL for "fref_variable(", else UNKNOWN.
// The token stream is left where it was.
S_TOKEN_TYPE COMPILER::DetectCallThrough()
{
    uint32_t code;
    const S_TOKEN_TYPE var_type = DetectUnknown(code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        return UNKNOWN;
    S_TOKEN_TYPE result = UNKNOWN;
    const S_TOKEN_TYPE next = Token.Get();
    if (next == OP_ARROW)
        result = CALL_METHOD;
    else if (next == OPEN_BRACKET)
    {
        S_TOKEN_TYPE type = UNKNOWN;
        if (var_type == VARIABLE)
        {
            if (const auto *var = VarTab.GetVarX(code))
                type = var->type;
        }
        else
        {
            LocalVarInfo lvi;
            if (FuncTab.GetVar(lvi, CurrentFuncCode, code))
                type = lvi.type;
        }
        if (type == VAR_FREF)
            result = CALL;
    }
    Token.StepBack(); // back to the identifier: its data again
    Token.StepBack();
    Token.Get();
    return result;
}

// object->Method(args): calls the function its class table names for Method with the object as
// the first argument; fref_variable(args): calls the function the reference names
bool COMPILER::CompileCallThrough(SEGMENT_DESC &Segment, S_TOKEN_TYPE call_type)
{
    uint32_t var_code;
    const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
    uint32_t func_args = 0;
    if (call_type == CALL)
    {
        Token.Get(); // '('
        if (!CompileCallArguments(Segment, func_args))
            return false;
        CompileToken(Segment, CALL);
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
        return true;
    }
    Token.Get(); // '->'
    if (Token.Get() != UNKNOWN || Token.GetData() == nullptr)
    {
        SetError("method name expected after '->'");
        return false;
    }
    const std::string method = Token.GetData();
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after '->%s'", method.c_str());
        return false;
    }
    // the object, by reference, is the first argument
    CompileToken(Segment, STACK_ALLOC);
    CompileToken(Segment, SETREF);
    CompileToken(Segment, STACK_TOP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    if (!CompileCallArguments(Segment, func_args))
        return false;
    func_args++;
    const uint32_t size = static_cast<uint32_t>(method.size() + 1);
    CompileToken(Segment, CALL_METHOD, 2, &size, sizeof(uint32_t), method.c_str(), size);
    CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
    return true;
}

// { key = value, "key" = value, classtable = GlobalObject }: a new object with these attributes
bool COMPILER::CompileObjectLiteral(SEGMENT_DESC &Segment)
{
    {
        // "{ a, b }" is an array literal: its first item is not "key ="
        char *const at = Token.GetProgramControl();
        const uint32_t line = DebugSourceLine;
        S_TOKEN_TYPE first = Token.Get();
        while (first == DEBUG_LINEFEED || first == COMMENT)
            first = Token.Get();
        const bool object = first == BLOCK_OUT || ((first == UNKNOWN || first == STRING) && Token.Get() == OP_EQUAL);
        Token.SetProgramControl(at);
        DebugSourceLine = line;
        if (!object)
            return CompileArrayLiteral(Segment);
    }
    CompileToken(Segment, OBJ_NEW);
    for (;;)
    {
        S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
        if (t == BLOCK_OUT)
            break;
        if (!((t == UNKNOWN || t == STRING) && Token.GetData()))
        {
            SetError("attribute name expected in object literal");
            return false;
        }
        std::string key = Token.GetData();
        if (CompileAuxiliaryTokens(Segment) != OP_EQUAL)
        {
            SetError("'=' expected after '%s' in object literal", key.c_str());
            return false;
        }
        if (storm::iEquals(key, "classtable"))
        {
            // the class table is a global object, stored by name (survives saving)
            uint32_t var_code;
            if (CompileAuxiliaryTokens(Segment) != UNKNOWN || DetectUnknown(var_code) != VARIABLE)
            {
                SetError("classtable must name a global object");
                return false;
            }
            const std::string table = Token.GetData();
            const uint32_t size = static_cast<uint32_t>(table.size() + 1);
            CompileToken(Segment, STACK_PUSH);
            CompileToken(Segment, STRING, 2, &size, sizeof(uint32_t), table.c_str(), size);
            CompileAuxiliaryTokens(Segment);
        }
        else if (!CompileExpression(Segment))
            return false;
        for (auto &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const uint32_t size = static_cast<uint32_t>(key.size() + 1);
        CompileToken(Segment, OBJ_SET, 2, &size, sizeof(uint32_t), key.c_str(), size);
        if (Token.GetType() == BLOCK_OUT)
            break;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in object literal");
            return false;
        }
    }
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after '[': is this "[a .. b]" (a slice of text, either bound optional)? The position is kept.
bool COMPILER::IsSlice()
{
    char *const at = Token.GetProgramControl();
    bool slice = false;
    S_TOKEN_TYPE previous = SQUARE_OPEN_BRACKET;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == END_OF_PROGRAMM || t == SEPARATOR || t == BLOCK_IN)
            break;
        if (t == SQUARE_OPEN_BRACKET || t == OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET || t == CLOSE_BRACKET)
        {
            if (depth-- == 0)
                break;
        }
        else if (t == DOT && previous == DOT && depth == 0)
        {
            slice = true;
            break;
        }
        if (t != DEBUG_LINEFEED && t != COMMENT)
            previous = t;
    }
    Token.SetProgramControl(at);
    return slice;
}

// [a .. b] after a variable (source) or after a bracketed expression (source STACK_TOP: the value on
// the stack); negative bounds count from the end; leaves the text slice on the stack
bool COMPILER::CompileSlice(SEGMENT_DESC &Segment, S_TOKEN_TYPE source, uint32_t source_code)
{
    uint32_t flags = 0;
    if (Token.Get() != DOT)
    {
        Token.StepBack();
        if (!CompileExpression(Segment))
            return false;
        flags |= 1;
    }
    if (Token.GetType() != DOT || Token.Get() != DOT)
    {
        SetError("'..' expected in slice");
        return false;
    }
    if (Token.Get() != SQUARE_CLOSE_BRACKET)
    {
        Token.StepBack();
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("']' expected after slice");
            return false;
        }
        flags |= 2;
    }
    CompileToken(Segment, OP_SLICE, 1, &flags, sizeof(uint32_t));
    if (source == STACK_TOP)
        CompileToken(Segment, STACK_TOP);
    else
        CompileToken(Segment, source, 1, &source_code, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// { a, b, c }: an array of the items' type
bool COMPILER::CompileArrayLiteral(SEGMENT_DESC &Segment)
{
    uint32_t count = 0;
    for (;;)
    {
        if (!CompileExpression(Segment))
            return false;
        count++;
        if (Token.GetType() == BLOCK_OUT)
            break;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in array literal");
            return false;
        }
    }
    CompileToken(Segment, ARRAY_BUILD, 1, &count, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after '(' of an attribute access: "(name)" with a variable compiles as ACCESS_VAR / ADVANCE_AP var,
// anything else is an expression (the position is kept)
bool COMPILER::IsAccessVariable()
{
    char *const at = Token.GetProgramControl();
    uint32_t code;
    const bool variable = Token.Get() == UNKNOWN &&
                          (DetectUnknown(code) == VARIABLE || DetectUnknown(code) == LOCAL_VARIABLE) &&
                          Token.Get() == CLOSE_BRACKET;
    Token.SetProgramControl(at - 1); // back on the '(' (one character) so that it is the current token
    Token.Get();
    return variable;
}

// after '[' following a variable: does "x[i]." (an attribute chain of an element) follow? Kept position.
bool COMPILER::IsIndexedChain()
{
    char *const at = Token.GetProgramControl();
    bool chain = false;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == END_OF_PROGRAMM || t == SEPARATOR || t == BLOCK_IN)
            break;
        if (t == SQUARE_OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET && depth-- == 0)
        {
            chain = Token.Get() == DOT;
            break;
        }
    }
    Token.SetProgramControl(at);
    return chain;
}

// on '(' of ".(expression)" in an rvalue chain: rAP saved on the stack while the expression runs
bool COMPILER::CompileAccessExpression(SEGMENT_DESC &Segment, uint32_t mode)
{
    CompileToken(Segment, PUSH_AP);
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("missing ')' after attribute expression");
        return false;
    }
    CompileToken(Segment, ADVANCE_AP_EXPR, 1, &mode, sizeof(uint32_t));
    return true;
}

// a chain element compiled for reading becomes quiet (a missing attribute is null, not an error)
void COMPILER::QuietChainElement(SEGMENT_DESC &Segment, uint32_t offset)
{
    if (static_cast<uint8_t>(Segment.pCode[offset]) == ADVANCE_AP)
    {
        Segment.pCode[offset] = ADVANCE_AP_QUIET;
        return;
    }
    // PUSH_AP (2 bytes), the expression, ADVANCE_AP_EXPR <mode>: find that instruction
    for (uint32_t ip = offset; ip < Segment.BCode_Program_size;)
    {
        const auto type = static_cast<uint8_t>(Segment.pCode[ip]);
        uint32_t size = static_cast<uint8_t>(Segment.pCode[ip + 1]);
        uint32_t data = ip + 2;
        if (size == 0xff)
        {
            memcpy(&size, &Segment.pCode[ip + 2], sizeof(uint32_t));
            data += sizeof(uint32_t);
        }
        if (type == ADVANCE_AP_EXPR)
        {
            const uint32_t quiet = kAccessQuiet;
            memcpy(&Segment.pCode[data], &quiet, sizeof(uint32_t));
            return;
        }
        ip = data + size;
    }
}

// &variable[index].chain: an attribute reference; mode kAccessCreate creates missing attributes
bool COMPILER::CompileAttributeAddress(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code, uint32_t mode)
{
    if (Token.GetType() == SQUARE_OPEN_BRACKET)
    {
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("missing ']'");
            return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, BX);
        CompileToken(Segment, MOVEAP_BXINDEX);
        Token.Get();
    }
    else
        CompileToken(Segment, MOVEAP);
    CompileToken(Segment, AP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    while (Token.GetType() == DOT)
    {
        Token.Get();
        if (Token.GetType() == OPEN_BRACKET)
        {
            if (!CompileAccessExpression(Segment, mode))
                return false;
        }
        else
        {
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            const uint32_t word = SCodec.Convert(Token.GetData());
            CompileToken(Segment, mode == kAccessCreate ? VERIFY_AP : ADVANCE_AP_QUIET);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &word, sizeof(uint32_t));
        }
        CompileAuxiliaryTokens(Segment);
    }
    CompileToken(Segment, PUSH_AP);
    return true;
}

bool COMPILER::IsAttributeIntrinsic(const char *name)
{
    return storm::iEquals(name, "touchattr") || storm::iEquals(name, "nameattr") ||
           storm::iEquals(name, "parentattr") || storm::iEquals(name, "deleteattr");
}

// touchattr(a.b) the attribute reference, creating missing attributes; nameattr(r) its name;
// parentattr(r) the attribute it belongs to; deleteattr(&a.b) removes it
bool COMPILER::CompileAttributeIntrinsic(SEGMENT_DESC &Segment)
{
    const std::string name = Token.GetData();
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after %s", name.c_str());
        return false;
    }
    if (storm::iEquals(name, "touchattr"))
    {
        uint32_t var_code;
        S_TOKEN_TYPE t = Token.Get();
        if (t == AND)
            t = Token.Get();
        const S_TOKEN_TYPE var_type = t == UNKNOWN ? DetectUnknown(var_code) : UNKNOWN;
        if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        {
            SetError("touchattr needs a variable and an attribute path");
            return false;
        }
        Token.Get();
        if (!CompileAttributeAddress(Segment, var_type, var_code, kAccessCreate))
            return false;
    }
    else
    {
        if (!CompileExpression(Segment))
            return false;
        if (storm::iEquals(name, "nameattr"))
        {
            const uint32_t func_code = FuncTab.FindFunc("GetAttributeName");
            const uint32_t args = 1;
            CompileToken(Segment, CALL_FUNCTION, 1, &func_code, sizeof(uint32_t));
            CompileToken(Segment, ARGS_NUM, 1, &args, sizeof(uint32_t));
        }
        else
            CompileToken(Segment, storm::iEquals(name, "parentattr") ? ATTR_PARENT : ATTR_DELETE);
    }
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after %s(...", name.c_str());
        return false;
    }
    CompileAuxiliaryTokens(Segment);
    return true;
}

bool COMPILER::IsArefVariable(S_TOKEN_TYPE var_type, uint32_t var_code)
{
    if (var_type == VARIABLE)
    {
        const auto *var = VarTab.GetVarX(var_code);
        return var && var->type == VAR_AREFERENCE && !var->IsArray();
    }
    LocalVarInfo lvi;
    return FuncTab.GetVar(lvi, CurrentFuncCode, var_code) && lvi.type == VAR_AREFERENCE && !lvi.IsArray();
}

// after the root of an attribute chain (rAP set, a value slot allocated, current token '.'):
// the rest of the chain, then the attribute's text, a.b$type(default) or a call a.b(args)
bool COMPILER::CompileChainTail(SEGMENT_DESC &Segment)
{
    S_TOKEN_TYPE sttResult;
    uint32_t dwVarCode;
    uint32_t dwAWCode;
    std::vector<uint32_t> chain; // offsets of the ADVANCE_AP instructions
    sttResult = Token.Get();
    do
    {
        // if(sttResult == OPEN_BRACKET)
        if (Token.GetType() == OPEN_BRACKET && !IsAccessVariable())
        {
            // .(expression): the attribute named by the expression's text
            chain.push_back(Segment.BCode_Program_size);
            if (!CompileAccessExpression(Segment, kAccessRead))
                return false;
        }
        else if (Token.GetType() == OPEN_BRACKET)
        {
            // access var
            sttResult = Token.Get();
            if (sttResult != UNKNOWN)
            {
                SetError("Invalid access var syntax");
                return false;
            }
            sttResult = DetectUnknown(dwVarCode);
            if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
            {
                SetError("not variable: %s", Token.GetData());
                return false;
            }
            chain.push_back(Segment.BCode_Program_size);
            CompileToken(Segment, ADVANCE_AP);
            CompileToken(Segment, sttResult, 1, &dwVarCode, sizeof(uint32_t));
            if (Token.Get() != CLOSE_BRACKET)
            {
                SetError("missing ')'");
                return false;
            }
        }
        else
        {
            // access word
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            dwAWCode = SCodec.Convert(Token.GetData());
            chain.push_back(Segment.BCode_Program_size);
            CompileToken(Segment, ADVANCE_AP);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &dwAWCode, sizeof(uint32_t));
        }
        sttResult = Token.Get();
        if (sttResult == DOT)
            Token.Get(); // get next token (after dot)
    } while (sttResult == DOT);

    if (Token.GetType() == OP_DOLLAR)
    {
        // a.b$int(0): the attribute converted to the type, or the default when it is missing
        for (const auto offset : chain)
            QuietChainElement(Segment, offset);
        CompileToken(Segment, STACK_POP_VOID); // the slot allocated for the value
        return CompileAttributeDefault(Segment);
    }

    if (Token.GetType() == OPEN_BRACKET)
    {
        // a.b(args): calls the function the attribute names (a function reference)
        CompileToken(Segment, STACK_WRITE);
        CompileToken(Segment, AP_VALUE);
        uint32_t func_args;
        if (!CompileCallArguments(Segment, func_args))
            return false;
        CompileToken(Segment, CALL_NAMED);
        CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    }

    // write attribute string to stack top
    CompileToken(Segment, STACK_WRITE);
    CompileToken(Segment, AP_VALUE);

    return true;
}

// on an identifier starting a statement: is it "name[i].chain(args)"? (the position is kept)
bool COMPILER::IsAttributeCall()
{
    uint32_t code;
    const S_TOKEN_TYPE var_type = DetectUnknown(code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        return false;
    char *const at = Token.GetProgramControl();
    const std::string name = Token.GetData();
    bool call = false, word = false, dot = false;
    auto skip = [this](S_TOKEN_TYPE open, S_TOKEN_TYPE close) {
        for (uint32_t depth = 0;;)
        {
            const S_TOKEN_TYPE t = Token.Get();
            if (t == END_OF_PROGRAMM || t == SEPARATOR)
                return false;
            if (t == open)
                depth++;
            else if (t == close && depth-- == 0)
                return true;
        }
    };
    for (bool more = true; more;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        switch (t)
        {
        case DOT:
            dot = true;
            word = false;
            break;
        case SQUARE_OPEN_BRACKET:
            more = skip(SQUARE_OPEN_BRACKET, SQUARE_CLOSE_BRACKET);
            word = false;
            break;
        case OPEN_BRACKET:
            if (dot && word)
            {
                call = true;
                more = false;
            }
            else
                more = dot && skip(OPEN_BRACKET, CLOSE_BRACKET); // .(expression)
            word = false;
            break;
        case DEBUG_LINEFEED:
        case COMMENT:
            break;
        default:
            if (dot && Token.GetData() && t != OP_EQUAL && t != SEPARATOR)
                word = true;
            else
                more = false;
            break;
        }
    }
    Token.SetProgramControl(at - name.size()); // back on the identifier (current token again)
    Token.Get();
    return call;
}
