#pragma once
#include "inststruct/instruction.h"
#include "inststruct/function.h"
#include "runtime/instruction.h"
#include "runtime/function.h"
#include "runtime/environment.h"
#include "virtualmachine.h"

namespace CVM
{
	namespace Compile
	{
		Runtime::Instruction Compile(const InstStruct::Instruction &inst, const InstStruct::Function &func);
		Runtime::Function Compile(const InstStruct::Function &func);

		Runtime::LocalEnvironment* CreateLoaclEnvironment(const InstStruct::Function &func, const TypeInfoMap &tim);
		Runtime::GlobalEnvironment* CreateGlobalEnvironment(size_t dysize, const TypeInfoMap &tim, const LiteralDataPool &datasmap);
	}
}
