/*
 * 寄存器式虚拟机 —— VM
 *
 * 执行由编译器生成的 Rua 寄存器字节码。
 *
 * 架构：
 *   值栈（stack）     — 存放所有函数帧，每帧是一组连续寄存器
 *   帧栈（frameStack）— 保存函数调用链的帧基址
 *   调用栈（callStack）— 保存返回地址
 *   寄存器 rX = stack[fp + X]
 *
 * 调用约定：
 *   参数通过 PUSH 传值，CALL 创建新帧
 *   RET 将结果写入调用者的 r0
 */

#include "虚拟机.h"
#include <iostream>
#include <sstream>
#include <cassert>
#include <cstring>
#ifdef _DEBUG
#include <chrono>
#endif

using std::string;
using std::vector;

// ==================== Value ====================

void Value::print() const
{
	if (type == ValueType::INTEGER)
		std::cout << data;
	else
		std::cout << "(string)";
}

// ==================== VMError ====================

VMError::VMError(const string &message) : std::runtime_error(message) {}

// ==================== VM ====================

VM::VM()
	: stackData(std::make_unique<Value[]>(STACK_CAP))
	, callStackData(std::make_unique<int[]>(CALL_CAP))
	, frameStackData(std::make_unique<int[]>(FRAME_CAP))
	, sp(-1), cp(-1), fpStack(-1), program(nullptr), ip(0)
{
}

void VM::run(const BytecodeProgram &prog)
{
	program = &prog;

	if (prog.code.empty())
		throw VMError("字节码为空");

	const auto &code = prog.code;
	const auto &constants = prog.constants;
	const auto &strings = prog.strings;
	const auto &functions = prog.functions;

	Value *const s = stackData.get();
	int *const cs = callStackData.get();
	int *const fs = frameStackData.get();

	int _sp = -1;
	int _cp = -1;
	int _fs = -1;

	vector<string> strPool;

	auto readI32 = [&](int addr) -> int
	{
		return static_cast<int>(code[addr])
		     | (static_cast<int>(code[addr + 1]) << 8)
		     | (static_cast<int>(code[addr + 2]) << 16)
		     | (static_cast<int>(code[addr + 3]) << 24);
	};

	auto getFP = [&]() -> int
	{
		return (_fs >= 0) ? fs[_fs] : 0;
	};

	auto greg = [&](int r) -> Value&
	{
		return s[getFP() + r];
	};

#ifdef _DEBUG
	auto 开始 = std::chrono::high_resolution_clock::now();
#endif

#define READ_I32() readI32(ip + 4)

	try
	{
		ip = 0;
		while (ip < static_cast<int>(code.size()))
		{
			Opcode op = static_cast<Opcode>(code[ip]);
			uint8_t rd  = code[ip + 1];
			uint8_t rs1 = code[ip + 2];
			uint8_t rs2 = code[ip + 3];
			int extra = readI32(ip + 4);

			switch (op)
			{
			case Opcode::HALT:
				goto done;

			case Opcode::MOVI:
				greg(rd) = Value(constants[extra]);
				ip += 8;
				break;

			case Opcode::MOVS:
				strPool.push_back(strings[extra]);
				greg(rd) = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
				ip += 8;
				break;

			case Opcode::MOV:
				greg(rd) = greg(rs1);
				ip += 8;
				break;

			case Opcode::ADD:
			{
				Value &a = greg(rd);
				Value &b = greg(rs1);
				Value &c = greg(rs2);
				if (b.type == ValueType::INTEGER && c.type == ValueType::INTEGER)
					a = Value(b.data + c.data);
				else if (b.type == ValueType::STRING && c.type == ValueType::STRING)
				{
					strPool.push_back(strPool[b.data] + strPool[c.data]);
					a = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
				}
				else
					throw VMError("运行时错误：ADD 操作数类型不匹配");
				ip += 8;
				break;
			}

			case Opcode::SUB:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：SUB 需要整数操作数");
				a = Value(b.data - c.data);
				ip += 8;
				break;
			}

			case Opcode::MUL:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：MUL 需要整数操作数");
				a = Value(b.data * c.data);
				ip += 8;
				break;
			}

			case Opcode::DIV:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：DIV 需要整数操作数");
				if (c.data == 0)
					throw VMError("运行时错误：除数为零");
				a = Value(b.data / c.data);
				ip += 8;
				break;
			}

			case Opcode::MOD:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：MOD 需要整数操作数");
				if (c.data == 0)
					throw VMError("运行时错误：除数为零");
				a = Value(b.data % c.data);
				ip += 8;
				break;
			}

			case Opcode::EQ:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != c.type)
					a = Value(0);
				else if (b.type == ValueType::INTEGER)
					a = Value(b.data == c.data ? 1 : 0);
				else
					a = Value(strPool[b.data] == strPool[c.data] ? 1 : 0);
				ip += 8;
				break;
			}

			case Opcode::NE:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != c.type)
					a = Value(1);
				else if (b.type == ValueType::INTEGER)
					a = Value(b.data != c.data ? 1 : 0);
				else
					a = Value(strPool[b.data] != strPool[c.data] ? 1 : 0);
				ip += 8;
				break;
			}

			case Opcode::LT:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：LT 需要整数操作数");
				a = Value(b.data < c.data ? 1 : 0);
				ip += 8;
				break;
			}

			case Opcode::GT:
			{
				Value &a = greg(rd), &b = greg(rs1), &c = greg(rs2);
				if (b.type != ValueType::INTEGER || c.type != ValueType::INTEGER)
					throw VMError("运行时错误：GT 需要整数操作数");
				a = Value(b.data > c.data ? 1 : 0);
				ip += 8;
				break;
			}

			case Opcode::JMP:
				ip += 8 + extra;
				break;

			case Opcode::JIF:
			{
				Value &cond = greg(rs1);
				if (cond.type != ValueType::INTEGER)
					throw VMError("运行时错误：JIF 需要整数条件");
				ip += (cond.data == 0) ? (8 + extra) : 8;
				break;
			}

			case Opcode::PUSH:
			{
				if (++_sp >= STACK_CAP)
					throw VMError("运行时错误：值栈溢出");
				s[_sp] = greg(rs1);
				ip += 8;
				break;
			}

			case Opcode::CALL:
			{
				int funcIdx = extra;
				if (funcIdx < 0 || funcIdx >= static_cast<int>(functions.size()))
				{
					std::ostringstream oss;
					oss << "运行时错误：无效的函数索引 " << funcIdx;
					throw VMError(oss.str());
				}
				const FunctionInfo &func = functions[funcIdx];

				// 新帧基址：第一个 PUSH 的是 r0 占位，后续是参数 r1..rN
				int newFP = _sp - func.paramCount;

				// 压入新帧基址（旧帧基址通过 _fs-1 隐式保留在帧栈中）
				if (++_fs >= FRAME_CAP)
					throw VMError("运行时错误：帧栈溢出");
				fs[_fs] = newFP;

				// 清零 r0 之后的局部变量和临时区
				// （r1..rN 已经是 PUSH 的参数值，r0 是占位）
				for (int i = func.paramCount + 1; i < func.regCount; i++)
					s[newFP + i] = Value(0);

				_sp = newFP + func.regCount - 1;

				// 保存返回地址
				if (++_cp >= CALL_CAP)
					throw VMError("运行时错误：调用栈溢出");
				cs[_cp] = ip + 8;

				ip = func.codeOffset;
				break;
			}

			case Opcode::RET:
			{
				Value retVal = greg(0);

				// 弹出当前帧
				int calleeFP = getFP();
				_sp = calleeFP - 1;

				// 恢复调用者帧
				--_fs;
				if (_fs >= 0)
					greg(0) = retVal;

				// 恢复返回地址
				if (_cp < 0)
					throw VMError("运行时错误：调用栈下溢");
				ip = cs[_cp--];
				break;
			}

			case Opcode::PRINT:
			{
				Value &v = greg(rs1);
				if (v.type == ValueType::INTEGER)
					std::cout << v.data;
				else
					std::cout << strPool[v.data];
				ip += 8;
				break;
			}

			default:
			{
				std::ostringstream oss;
				oss << "运行时错误：未知操作码 0x"
				    << std::hex << static_cast<int>(op) << std::dec;
				throw VMError(oss.str());
			}
			}
		}

	done:
		sp = _sp;
		cp = _cp;
		fpStack = _fs;
	}
	catch (const VMError &e)
	{
		std::cerr << "\n" << e.what() << std::endl;
		sp = _sp;
		cp = _cp;
		fpStack = _fs;
	}

#ifdef _DEBUG
	{
		auto 结束 = std::chrono::high_resolution_clock::now();
		auto 耗时 = std::chrono::duration_cast<std::chrono::microseconds>(结束 - 开始).count();
		std::cout << "\n[DEBUG] 字节码执行耗时: " << 耗时 << " 微秒 (" << (耗时 / 1000.0) << " 毫秒)\n";
	}
#endif
}

#undef READ_I32
