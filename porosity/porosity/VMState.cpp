#include "Porosity.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

#define TaintStackEntry(x) (m_stack[x].type |= VMState::StackRegisterType::UserInputTainted)
#define TagStackEntryAsConstant(x) (m_stack[x].type |= VMState::StackRegisterType::Constant)
#define IsStackEntryTainted(x) (m_stack[x].type & (UserInput | UserInputTainted))
#define IsStackEntryTypeTainted(type) (type & (UserInput | UserInputTainted))

inline void 
VMState::SwapStackRegisters(
    uint32_t index_a,
    uint32_t index_b
)
{
    StackRegister oldValue = m_stack[index_a];
    StackRegister newValue = m_stack[index_b];

    m_stack[index_b] = oldValue;
    m_stack[index_a] = newValue;
}

std::string random_string(size_t length)
{
    auto randchar = []() -> char
    {
        const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
}

string
VMState::getDepth(
    void
)
{
    stringstream tabs;
    for (int i = 0; i < m_depthLevel; i+= 1) tabs << "    ";
    return tabs.str();
}

void
VMState::displayStack(
    void
) {
    printf("EIP: 0x%x STACK: \n", m_eip);

    int regIndex = 0;

    for (auto it = m_stack.begin(); it != m_stack.end(); ++it) {
        u256 data = it->value;
        printf("%08X: ", regIndex);
        std::cout << std::setfill('0') << std::setw(64) << std::hex << data;

        printf(" %s", it->type & UserInput ? "(U)" : "");
        printf(" %s", it->type & UserInputTainted ? "(T)" : "");

        printf(" [name = %s]", it->name.c_str());
        printf("\n");

        regIndex++;
    }
}

void
VMState::pushStack(
    StackRegister data
) {
    if (data.name.empty()) data.name = "var_" + random_string(5);
    m_stack.insert(m_stack.begin(), data);
}

void
VMState::popStack(
    void
)
{
    m_stack.erase(m_stack.begin());
}

VMState::Expression
VMState::getCurrentExpression(
    Instruction _instr
) {
    Expression exp;

    InstructionInfo info = dev::eth::instructionInfo(_instr);

    if (!dev::eth::isValidInstruction(_instr)) {
        printf("%02X                    !INVALID!", int(_instr));
        exp.name = "INVALID INSTRUCTION";
        return exp;
    }

    switch (info.args) {
        case 0:
            exp = getExpressionForInstruction(_instr, 0, 0, 0);
        break;
        case 1:
            exp = getExpressionForInstruction(_instr, &m_stack[0], 0, 0);
        break;
        case 2:
            exp = getExpressionForInstruction(_instr, &m_stack[0], &m_stack[1], 0);
            break;
        case 3:
            exp = getExpressionForInstruction(_instr, &m_stack[0], &m_stack[1], &m_stack[2]);
        break;
    }

    if (exp.name.size()) {
        if (VERBOSE_LEVEL >= 3) printf("%s: ", __FUNCTION__);
        if (VERBOSE_LEVEL >= 2) printf("%s\n", exp.name.c_str());
    }

    return exp;
}

string
VMState::getDismangledRegisterName(
    StackRegister *first
) {
    switch (first->type) {
        case UserInput:
        case UserInputTainted:
        case StorageType:
        case ConstantComputed:
            return first->name;
        break;
        case RegTypeFlag:
            // TODO: point to Expresion *
        break;
        case Constant:
        {
            stringstream mod;
            mod << "0x" << std::hex << first->value;
            return mod.str();
            // mod << "0x" << std::hex << second->value;
            break;
        }
        case RegTypeLabelCaller:
            return "msg.sender";
            break;
        case RegTypeLabelBlockHash:
            return "blockhash";
            break;
        default:
            // return first->name;
        break;
    }

    if (VERBOSE_LEVEL >= 5) printf("%s: unsupported type (type = 0x%x).\n", __FUNCTION__, first->type);
    return first->name;
}

VMState::Expression
VMState::getExpressionForInstruction(
    Instruction _instr,
    StackRegister *first,
    StackRegister *second,
    StackRegister *third
) {
    string exp;
    Expression result;

    result.instr = _instr;
    if (first) first->exp.instr = _instr;

    switch (_instr) {
        case Instruction::CALLDATALOAD:
        {
#if 0
            stringstream argname;
            uint32_t offset = int(first->value);
            argname << "arg_";
            argname << std::hex << offset;

            exp << first->name.c_str();
            exp << " = ";
            exp << argname.str().c_str();
            exp << ";";
#endif
            break;
        }
        case Instruction::NOT:
        {
            exp = first->name + " = ~" + getDismangledRegisterName(first) + ";";
            break;
        }
        case Instruction::ADD:
        case Instruction::MUL:
        case Instruction::SUB:
        case Instruction::DIV:
        case Instruction::SDIV:
        case Instruction::MOD:
        case Instruction::SMOD:
        case Instruction::EXP:
        {
            char *operation[] = { "+", "*", "-", "/", "/", "%%", "%%", "invld", "invld", "**", 0 };
            int index = int(_instr) - int(Instruction::ADD);
            exp = first->name + " = " + getDismangledRegisterName(first) + " " 
                + operation[index] + " " + getDismangledRegisterName(second) + ";";
            break;
        }
        case Instruction::LT:
        case Instruction::GT:
        case Instruction::SLT:
        case Instruction::SGT:
        case Instruction::EQ:
        {
            char *operation[] = { "<", ">", "<", ">", "==", 0 };
            stringstream mod;
            bool isSigned = ((_instr == Instruction::SLT) || (_instr == Instruction::SGT));
            string signedParam = isSigned ? "(signed)" : "";

            int index = int(_instr) - int(Instruction::LT);
            exp = "(" + signedParam + getDismangledRegisterName(first) + " " + operation[index] + " " + signedParam + getDismangledRegisterName(second) + ")";

            first->exp.name = exp;
            first->exp.instr = _instr;

            exp = "";
            break;
        }
        case Instruction::ISZERO:
        {
            // change name
            // change labeltype
            // tag expression to value stack.
            // m_jmpFlag = (m_stack[0].value == 0);

            string cond;
            if (first->exp.name.size()) cond = first->exp.name;
            else cond = getDismangledRegisterName(first);
            exp = "(!(" + cond + "))";

            first->exp.name = exp;
            exp = "";

            break;
        }
        case Instruction::JUMPI:
        {
            exp = "if (!" + second->exp.name + ") ";
            break;
        }
        case Instruction::ADDMOD:
        case Instruction::MULMOD:
        {
            char *operation[] = { "+", "*", 0 };
            int index = int(_instr) - int(Instruction::ADDMOD);
            exp = first->name + " = (" + getDismangledRegisterName(first) + " " + operation[index];
            exp += " " + getDismangledRegisterName(second) + ") %% " + getDismangledRegisterName(third) + ";";
            break;
        }
        case Instruction::SSTORE:
        {
            stringstream argname;
            uint32_t offset = int(first->value);
            argname << "stor_";
            argname << std::hex << offset;

            exp = argname.str() + " = " + getDismangledRegisterName(second) + ";";
            break;
        }
        case Instruction::MSTORE:
        {
            stringstream argname;
            uint32_t offset = int(first->value);
            argname << "mem_";
            argname << std::hex << offset;

            exp = argname.str() + " = " + getDismangledRegisterName(second) + ";";
            break;
        }
        case Instruction::SLOAD:
        {
            /*
            stringstream argname;
            uint32_t offset = int(first->value);
            argname << "store_";
            argname << std::hex << offset;
            first->name = argname.str();

            exp = argname.str() + " = " + getDismangledRegisterName(second) + ";";
            */
            break;
        }
        case Instruction::RETURN:
            exp = "return " + first->name + ";";
            break;
        default:
        break;
    }

    // result.name = exp;

    if (exp.size()) 
    {
        result.name = getDepth() + exp;
        // result << "\n"; // ?
    }

    return result;
}

bool
VMState::executeInstruction(
    uint32_t _offset,
    Instruction _instr,
    u256 const& _data
) {
    InstructionInfo info = dev::eth::instructionInfo(_instr);

    if (!dev::eth::isValidInstruction(_instr)) {
        printf("%02X                    !INVALID!", int(_instr));
        return false;
    }

    switch (_instr) {
        case Instruction::PUSH1:
        case Instruction::PUSH2:
        case Instruction::PUSH3:
        case Instruction::PUSH4:
        case Instruction::PUSH5:
        case Instruction::PUSH6:
        case Instruction::PUSH7:
        case Instruction::PUSH8:
        case Instruction::PUSH9:
        case Instruction::PUSH10:
        case Instruction::PUSH11:
        case Instruction::PUSH12:
        case Instruction::PUSH13:
        case Instruction::PUSH14:
        case Instruction::PUSH15:
        case Instruction::PUSH16:
        case Instruction::PUSH17:
        case Instruction::PUSH18:
        case Instruction::PUSH19:
        case Instruction::PUSH20:
        case Instruction::PUSH21:
        case Instruction::PUSH22:
        case Instruction::PUSH23:
        case Instruction::PUSH24:
        case Instruction::PUSH25:
        case Instruction::PUSH26:
        case Instruction::PUSH27:
        case Instruction::PUSH28:
        case Instruction::PUSH29:
        case Instruction::PUSH30:
        case Instruction::PUSH31:
        case Instruction::PUSH32:
        {
            StackRegister reg = { "", Constant, 0, 0 };
            reg.value = _data;
            reg.type = Constant;
            pushStack(reg);
            break;
        }
        case Instruction::CALLDATALOAD:
        {
            // https://github.com/ethereum/cpp-ethereum/blob/develop/libevm/VM.cpp#L659
            // 0x0 -> 0x04 (magic code associated to function hash)
            // e.g. 0xcdcd77c000000000000000000000000000000000000000000000000000000000 /( 2^0xe0) -> 0xcdcd77c0
            // argv[0]: 0x04 -> 0x24 - 256bits (32 bytes)
            // argv[1]: 0x24 -> 0x44 - 256bits (32 bytes)
            // m_stack.erase(m_stack.begin());
            StackRegister reg = { "", UserInput, 0, 0 };

            reg.type = UserInput;
            uint32_t offset = int(m_stack[0].value);
            reg.offset = offset;

            if (m_data.size()) {
                if (offset + 31 < m_data.size())
                    reg.value = (u256)*(h256 const*)(m_data.data() + (size_t)offset);
                else if (reg.value >= m_data.size())
                    reg.value = u256(0); // invalid
            }
            else {
                //
                // In case the caller didn't provide any input parameters
                //
            }

            stringstream argname;
            argname << "arg_";
            argname << std::hex << offset;
            reg.name = argname.str();
            m_stack[0] = reg;
            break;
        }
        case Instruction::MSTORE:
            setMemoryData(int(m_stack[0].value), m_stack[1].value);
            popStack();
            popStack();
        break;
        case Instruction::SSTORE:
            // TODO:
            popStack();
            popStack();
        break;
        case Instruction::SLOAD:
        {
            stringstream argname;
            uint32_t offset = int(m_stack[0].value);
            argname << "store_";
            argname << std::hex << offset;
            m_stack[0].name = argname.str();
            m_stack[0].type = StorageType;
            break;
        }
        case Instruction::LOG0:
        case Instruction::LOG1:
        case Instruction::LOG2:
        case Instruction::LOG3:
        case Instruction::LOG4:
        {
            popStack();
            popStack();
            int itemsToPop = int(_instr) - int(Instruction::LOG0);
            for (int i = 0; i < itemsToPop; i++) popStack();
            break;
        }
        case Instruction::DUP1:
        case Instruction::DUP2:
        case Instruction::DUP3:
        case Instruction::DUP4:
        case Instruction::DUP5:
        case Instruction::DUP6:
        case Instruction::DUP7:
        case Instruction::DUP8:
        case Instruction::DUP9:
        case Instruction::DUP10:
        case Instruction::DUP11:
        case Instruction::DUP12:
        case Instruction::DUP13:
        case Instruction::DUP14:
        case Instruction::DUP15:
        case Instruction::DUP16:
        {
            uint8_t index = int(_instr) - int(Instruction::DUP1);
            pushStack(m_stack[index]);
            break;
        }
        case Instruction::SWAP1:
        case Instruction::SWAP2:
        case Instruction::SWAP3:
        case Instruction::SWAP4:
        case Instruction::SWAP5:
        case Instruction::SWAP6:
        case Instruction::SWAP7:
        case Instruction::SWAP8:
        case Instruction::SWAP9:
        case Instruction::SWAP10:
        case Instruction::SWAP11:
        case Instruction::SWAP12:
        case Instruction::SWAP13:
        case Instruction::SWAP14:
        case Instruction::SWAP15:
        case Instruction::SWAP16:
        {
            uint8_t index = int(_instr) - (int(Instruction::SWAP1) - 1);

            // printf("SWAP(stack[0], stack[%d])\n", index);
            StackRegister oldValue = m_stack[index];
            StackRegister newValue = m_stack[0];

            m_stack[0] = oldValue;
            m_stack[index] = newValue;
            break;
        }
        case Instruction::POP:
            popStack();
        break;
        case Instruction::SUB:
            m_stack[0].value = m_stack[0].value - m_stack[1].value;
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack();
            break;
        case Instruction::DIV:
            if (!m_stack[1].value) m_stack[0].value = 0;
            else m_stack[0].value = m_stack[0].value / m_stack[1].value;
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack();
            break;
        case Instruction::ADD:
            m_stack[0].value = m_stack[0].value + m_stack[1].value;
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack();
            break;
        case Instruction::MUL:
            m_stack[0].value = m_stack[0].value * m_stack[1].value;
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack();
            break;
        break;
        case Instruction::MOD:
            m_stack[0].value = m_stack[0].value % m_stack[1].value;
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack();
            break;
        case Instruction::EXP:
            m_stack[0].value = porosity::exp256(m_stack[0].value, m_stack[1].value);
            if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[0].type = ConstantComputed;
            m_stack[1] = m_stack[0];
            popStack();
            break;
        case Instruction::AND:
        {
            u256 address_mask("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
            m_stack[0].value = m_stack[0].value & m_stack[1].value;
            if (m_stack[0].value.compare(address_mask))
            {
                // mask for address.
                m_stack[0].type = m_stack[1].type; // copy mask to result.
                m_stack[0].name = m_stack[1].name;
            }
            // if (IsStackEntryTainted(1)) TaintStackEntry(0);
            m_stack[1] = m_stack[0];
            popStack(); // info.ret
            break;
        }
        case Instruction::EQ:
            // change name
            // change labeltype
            // tag expression to value stack.
            m_stack[1].type = RegTypeFlag;
            m_stack[1].value = (m_stack[1].value == m_stack[0].value);
            m_stack[1].exp = m_stack[0].exp;
            // m_jmpFlag = int(m_stack[1].value);
            popStack();
            break;
        case Instruction::LT:
            // change name
            // change labeltype
            // tag expression to value stack.
            m_stack[1].type = RegTypeFlag;
            m_stack[1].value = (m_stack[0].value < m_stack[1].value);
            m_stack[1].exp = m_stack[0].exp;
            // m_jmpFlag = int(m_stack[1].value);
            popStack();
            break;
        case Instruction::GT:
            // change name
            // change labeltype
            // tag expression to value stack.
            m_stack[1].type = RegTypeFlag;
            m_stack[1].value = (m_stack[0].value > m_stack[1].value);
            m_stack[1].exp = m_stack[0].exp;
            // m_jmpFlag = int(m_stack[1].value);
            popStack();
            break;
        case Instruction::ISZERO:
        {
            m_stack[0].value = (m_stack[0].value == 0);
            break;
        }
        case Instruction::JUMP:
            m_eip = int(m_stack[0].value);
            popStack();
            return true;
        break;
        case Instruction::JUMPI:
        {
            int jmpTarget = int(m_stack[0].value);
            int nextInstr = (m_eip + sizeof(Instruction) + info.additional);
            int cond = int(m_stack[1].value);

            popStack();
            popStack();

            if (cond) {
                if (VERBOSE_LEVEL > 4) printf("**** FORK BEGIN****\n");
                // We force the opposite condition for discovery.
                // requires additional coverage.
                VMState state = *this;
                state.m_depthLevel++;
                bytes subBlock(m_byteCodeRuntimeRef->begin(), m_byteCodeRuntimeRef->end()); 
                state.m_eip = nextInstr;
                state.executeByteCode(&subBlock);

                // Resume execution.
                if (VERBOSE_LEVEL > 4) printf("**** FORK END****\n");
                m_eip = jmpTarget;
                return true;
            }
            else {
                if (VERBOSE_LEVEL > 4) printf("**** FORK BEGIN****\n");
                // We force the opposite condition for discovery.
                // requires additional coverage.
                VMState state = *this;
                state.m_depthLevel++;
                bytes subBlock(m_byteCodeRuntimeRef->begin(), m_byteCodeRuntimeRef->end());
                state.m_eip = jmpTarget;
                state.executeByteCode(&subBlock);

                // resume execution.
                if (VERBOSE_LEVEL > 4) printf("**** FORK END****\n");
                m_eip = nextInstr;
                return true;
            }
            break;
        }
        case Instruction::JUMPDEST:
            // new basic block
            break;
        case Instruction::INVALID:
        case Instruction::STOP:
        case Instruction::SUICIDE:
            // done
            return false;
        break;
        case Instruction::RETURN:
            return false;
        break;
        case Instruction::SHA3:
        {
            uint64_t offset = (uint64_t)m_stack[2].value;
            uint64_t size = (uint64_t)m_stack[1].value;
            // stack[0] = sha3(memStorage + offset, size);
            popStack();
            // popStack();
            m_stack[0].value = u256(dev::keccak256("OKLM"));
            m_stack[0].type = RegTypeLabelSha3;
            break;
        }
        case Instruction::PC:
        {
            stringstream argname;
            argname << "loc_";
            argname << std::hex << m_eip;

            m_stack[0].type = RegTypeLabel;
            m_stack[0].name = argname.str();
            m_stack[0].value = m_eip;
            break;
        }
        case Instruction::CALLER:
        {
            u256 data = _data;
            StackRegister reg = { "", RegTypeLabelCaller, 0, 0 };
            reg.value = m_caller;
            reg.name = "msg.sender";
            reg.type = RegTypeLabelCaller;
            pushStack(reg);
            break;
        }
        case Instruction::BLOCKHASH:
        {
            u256 data = _data;
            StackRegister reg = { "", RegTypeLabelBlockHash, 0, 0 };
            reg.value = u256("0xdeadbeefdeadbeefdeadbeefdeadbeef");
            reg.name = "blockhash";
            reg.type = RegTypeLabelBlockHash;
            pushStack(reg);
            break;
        }
        default:
            printf("%s: NOT_IMPLEMENTED: %s\n", __FUNCTION__, info.name.c_str());
            return false;
            break;
    }

    // if (VERBOSE_LEVEL >= 4) displayStack();

    m_eip += sizeof(Instruction) + info.additional;

    return true;
}

void
VMState::setData(
    bytes _data
)
{
    m_data = _data;
}

void
VMState::setMemoryData(
    uint16_t _offset,
    u256 _data
) {
    u256 data = _data;
    u256 arg = 0;
    /*
    for (int i = 0; i < 32; ++i) {
        uint8_t dataByte = int(data & 0xFF);
        data >>= 8;
        m_mem[_offset + i] |= dataByte;
    }*/
}

void
VMState::executeByteCode(
    bytes *_byteCodeRuntime
) {
    m_byteCodeRuntimeRef = _byteCodeRuntime;

    if (m_depthLevel > 4) return; // limit

    while (true)
    {
        auto it = _byteCodeRuntime->begin() + m_eip;
        Instruction instr = Instruction(*it);
        size_t additional = 0;
        if (isValidInstruction(instr)) additional = instructionInfo(instr).additional;

        uint32_t offset = std::distance(_byteCodeRuntime->begin(), it);
        u256 data;

        for (size_t i = 0; i < additional; ++i)
        {
            data <<= 8;
            if (++it < m_byteCodeRuntimeRef->end()) {
                data |= *it;
            }
        }


        if (VERBOSE_LEVEL >= 3) {
            printf("=================\n");
            printf("BEFORE:\n");
            displayStack();
        }
        if (VERBOSE_LEVEL >= 2) porosity::printInstruction(offset, instr, data);
        Expression exp = getCurrentExpression(instr);
        if (exp.name.size()) printf("%s\n", exp.name.c_str());
        bool ret = executeInstruction(offset, instr, data);
        if (VERBOSE_LEVEL >= 3) {
            printf("AFTER:\n");
            displayStack();
            printf("=================\n");
        }
        if (VERBOSE_LEVEL >= 6) getchar();
        if (!ret || (m_eip == m_byteCodeRuntimeRef->size())) break;
    }
}