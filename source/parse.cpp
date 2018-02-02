#include "basic.h"
#include "parse.h"
#include "inststruct/instcode.h"
#include "inststruct/function.h"
#include "inststruct/instpart.h"
#include <regex>

namespace CVM
{
	using ParsedIdentifier = PriLib::ExplicitType<std::string>;

	enum ParseErrorCode
	{
		PEC_NumTooLarge,
		PEC_URNum,
		PEC_URIns,
		PEC_URCmd,
		PEC_UREnv,
		PEC_URReg,
		PEC_UREscape,
		PEC_UFType,
		PEC_UFFunc,
		PEC_DUType,
		PEC_DUFunc,
	};

	class ParseInfo
	{
	public:
		explicit ParseInfo(TypeInfoMap &tim)
			: tim(tim) {}

		std::map<std::string, InstStruct::FunctionInfo*> functable;
		InstStruct::FunctionInfo *currfunc;
		TypeInfoMap &tim;
		size_t lcount = 0;
		ParsedIdentifier entry;
		ParsedIdentifier currtype;
		int currsection = 0;

		void putErrorLine() const {
			fprintf(stderr, "Parse Error in line(%zu).\n", lcount);
		}
		void putErrorLine(ParseErrorCode pec) const {
			fprintf(stderr, "Parse Error for '%s' in line(%zu).\n", geterrmsg(pec), lcount);
		}
		void putErrorLine(ParseErrorCode pec, const std::string &msg) const {
			fprintf(stderr, "Parse Error for '%s' at '%s' in line(%zu).\n", geterrmsg(pec), msg.c_str(), lcount);
		}
		void putError(const std::string &msg) const {
			fprintf(stderr, "%s\n", msg.c_str());
		}

		const char* geterrmsg(ParseErrorCode pec) const {
			static std::map<ParseErrorCode, const char*> pecmap = {
				{ PEC_NumTooLarge, "Number too large" },
				{ PEC_URNum, "Unrecognized Number" },
				{ PEC_URCmd, "Unrecognized command" },
				{ PEC_URIns, "Unrecognized instruction" },
				{ PEC_UREnv, "Unrecognized environment" },
				{ PEC_URReg, "Unrecognized register" },
				{ PEC_UREscape, "Unrecognized escape" },
				{ PEC_UFType, "Unfind type" },
				{ PEC_UFFunc, "Unfind function" },
				{ PEC_DUType, "type name duplicate" },
				{ PEC_DUFunc, "func name duplicate" },
			};
			return pecmap.at(pec);
		}
	};

	PriLib::StorePtr<ParseInfo> createParseInfo(TypeInfoMap &tim) {
		return PriLib::StorePtr<ParseInfo>(new ParseInfo(tim));
	}

	InstStruct::Register parseRegister(ParseInfo &parseinfo, const std::string &word) {
		if (word[0] != '%') {
			parseinfo.putErrorLine();
			return InstStruct::Register();
		}
		if (word == "%res")
			return InstStruct::Register(InstStruct::r_res);
		else if (word == "%0")
			return InstStruct::Register(InstStruct::r_0);

		InstStruct::RegisterType rtype;
		InstStruct::EnvType etype;
		uint16_t index;

		const char *mword = word.c_str() + 1;

		switch (word[1]) {
		case 'g':
			rtype = InstStruct::r_g;
			mword++;
			break;
		case 't':
			rtype = InstStruct::r_t;
			mword++;
			break;
		default:
			if (std::isdigit(word[1])) {
				rtype = InstStruct::r_n;
				break;
			}
			else {
				parseinfo.putErrorLine();
				break;
			}
		}

		std::regex rc("(\\d+)");
		std::regex re("(\\d+)\\(\\%(\\w+)\\)");

		std::cmatch cm;
		if (std::regex_match(mword, cm, rc)) {
			index = PriLib::Convert::to_integer<uint16_t>(cm[1].str(), [&]() { parseinfo.putErrorLine(PEC_NumTooLarge); });
			etype = InstStruct::e_current;
		}
		else if ((std::regex_match(mword, cm, re))) {
			index = PriLib::Convert::to_integer<uint16_t>(cm[1].str(), [&]() { parseinfo.putErrorLine(PEC_NumTooLarge); });
			auto estr = cm[2].str();
			if (estr == "env") {
				etype = InstStruct::e_current;
			}
			else if (estr == "tenv") {
				etype = InstStruct::e_temp;
			}
			else if (estr == "penv") {
				etype = InstStruct::e_parent;
			}
			else {
				parseinfo.putErrorLine(PEC_UREnv);
			}
		}
		else {
			parseinfo.putErrorLine(PEC_URReg);
		}

		return InstStruct::Register(rtype, etype, index);
	}

	InstStruct::Data parseDataInst(ParseInfo &parseinfo, const std::string &word) {
		auto errfunc = [&]() {
			parseinfo.putErrorLine(PEC_URNum, word);
			parseinfo.putError("The number must be unsigned integer and below " + std::to_string(8 * sizeof(InstStruct::Data::Type)) + "bits.");
		};

		std::string nword = word;
		int base = 10;

		if (word.length() > 2 && word[0] == '0' && word[1] == 'x') {
			base = 16;
			nword = word.substr(2);
		}

		return InstStruct::Data(PriLib::Convert::to_integer<InstStruct::Data::Type>(nword, errfunc, base));
	}

	ParsedIdentifier parseIdentifier(ParseInfo &parseinfo, const std::string &word) {
		std::string mword;
		bool escape = false; // Use '%' in identifier.
		for (auto &c : word) {
			if (escape) {
				escape = false;
				if (c == '%' || c == '#')
					mword.push_back(c);
				else
					parseinfo.putErrorLine(PEC_UREscape);
			}
			else {
				if (c == '%')
					escape = true;
				else
					mword.push_back(c);
			}
		}
		if (escape) {
			parseinfo.putErrorLine(PEC_UREscape);
		}
		PriLib::Convert::split(mword, "#", [&](const char *w) {
			//println(w);
			});
		return ParsedIdentifier(mword);
	}

	void parseLineBase(
		ParseInfo &parseinfo,
		const std::string &line,
		std::function<int(const char*)> f1,
		std::function<void(ParseInfo&, int, const std::vector<std::string>&)> f2)
	{
		const char *blanks = " \t,";
		bool start = true;
		int code = 0;
		std::vector<std::string> list;
		PriLib::Convert::split(line, blanks, [&](const char *s) {
			if (start) {
				code = f1(s);
				start = false;
			}
			else {
				list.push_back(s);
			}
			});
		f2(parseinfo, code, list);
	}

	TypeIndex parseType(ParseInfo &parseinfo, const std::string &word) {
		TypeIndex index;
		if (parseinfo.tim.find(parseIdentifier(parseinfo, word).data, index)) {
			return index;
		}
		else {
			parseinfo.putErrorLine(PEC_UFType);
			return TypeIndex(0);
		}
	}
}

namespace CVM
{
	enum KeySection : int {
		ks_nil = 0,
		ks_program,
		ks_imports,
		ks_exports,
		ks_datas,
		ks_module,
		ks_func,
		ks_type,
	};

	const auto& getSectionKeyMap() {
		static std::map<std::string, int> map {
			{ "program", ks_program },
			{ "imports", ks_imports },
			{ "exports", ks_exports },
			{ "datas", ks_datas },
			{ "module", ks_module },
			{ "func", ks_func },
			{ "type", ks_type },
		};
		return map;
	}
	void parseSection(ParseInfo &parseinfo, int code, const std::vector<std::string> &list) {
		switch (code) {
		case ks_program:
			break;
		case ks_func:
		{
			if (list.size() != 1) {
				parseinfo.putErrorLine();
			}
			const auto &name = parseIdentifier(parseinfo, list.at(0));
			if (parseinfo.functable.find(name.data) == parseinfo.functable.end()) {
				auto fp = new InstStruct::FunctionInfo();
				parseinfo.functable[list[0]] = fp;
				parseinfo.currfunc = fp;
			}
			else {
				parseinfo.putErrorLine(PEC_DUFunc);
			}
			break;
		}
		case ks_type:
		{
			if (list.size() != 1) {
				parseinfo.putErrorLine();
			}
			const auto &name = parseIdentifier(parseinfo, list.at(0));
			TypeIndex tid;
			if (parseinfo.tim.find(name.data, tid)) {
				parseinfo.putErrorLine(PEC_DUType);
			}
			else {
				parseinfo.currtype = name;
				parseinfo.tim.insert(name.data, TypeInfo());
			}
			break;
		}
		}
	}

	void parseSectionInside(ParseInfo &parseinfo, const std::string &code, const std::vector<std::string> &list) {
		using ParseInsideProcess = std::function<void(ParseInfo &, const std::vector<std::string> &)>;
		using ParseInsideMap = std::map<std::string, ParseInsideProcess>;
		static std::map<int, ParseInsideMap> parsemap {
			{
				ks_func,
				ParseInsideMap {
					{
						"arg",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
						}
					},
					{
						"dyvarb",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
							if (list.size() == 1) {
								parseinfo.currfunc->dyvarb_count = PriLib::Convert::to_integer<size_t>(list[0], [&]() { parseinfo.putErrorLine(PEC_URNum); });
							}
							else {
								parseinfo.putErrorLine();
							}
						}
					},
					{
						"stvarb",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
							if (list.size() == 2) {
								auto &type = list[list.size() - 1];
								size_t count = PriLib::Convert::to_integer<size_t>(list[0], [&]() { parseinfo.putErrorLine(PEC_URNum); });
								TypeIndex index = parseType(parseinfo, type);
								for (size_t i = 0; i < count; ++i)
									parseinfo.currfunc->stvarb_typelist.push_back(index);
							}
							else {
								parseinfo.putErrorLine();
							}
						}
					},
					{
						"data",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
						}
					},
				}
			},
			{
				ks_program,
				ParseInsideMap {
					{
						"entry",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
							if (list.size() == 1) {
								parseinfo.entry = parseIdentifier(parseinfo, list[0]);
							}
							else {
								parseinfo.putErrorLine();
							}
						}
					},
				},
			},
			{
				ks_type,
				ParseInsideMap {
					{
						"size",
						[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
							const auto &name = parseinfo.currtype;
							auto &typeinfo = parseinfo.tim.at(name.data);
							if (list.size() == 1) {
								bool success = PriLib::Convert::to_integer(list[0], typeinfo.size.data);
								if (!success)
									parseinfo.putErrorLine();
							}
							else {
								parseinfo.putErrorLine();
							}
						}
					},
				},
			},
		};

		auto iter = parsemap.find(parseinfo.currsection);
		if (iter != parsemap.end()) {
			auto iiter = iter->second.find(code);
			if (iiter != iter->second.end()) {
				iiter->second(parseinfo, list);
			}
			else {
				parseinfo.putErrorLine(PEC_URCmd);
			}
		}
		else {
			parseinfo.putErrorLine(PEC_URCmd);
		}
	}

	InstStruct::Instruction* parseFuncInstBase(ParseInfo& parseinfo, const std::string &code, const std::vector<std::string> &list);

	void parseFuncInst(ParseInfo &parseinfo, const std::string &code, const std::vector<std::string> &list)
	{
		return parseinfo.currfunc->instdata.push_back(parseFuncInstBase(parseinfo, code, list));
	}

	void parseLine(ParseInfo &parseinfo, const std::string &line)
	{
		char fc = line[0];

		if (fc == '.') {
			parseLineBase(parseinfo, line,
				[&](const char *code) {
					auto &map = getSectionKeyMap();
					auto iter = map.find(code + 1);
					if (iter != map.end()) {
						parseinfo.currsection = iter->second;
						return iter->second;
					}
					else
						parseinfo.putErrorLine();
					return 0;
				},
				parseSection);
		}
		else if (std::isblank(fc)) {
			std::string cmd;
			parseLineBase(parseinfo, line,
				[&](const char *code) {
					int isinst = code[0] != '.';
					cmd = isinst ? code : code + 1;
					return isinst;
				},
				[&](ParseInfo &parseinfo, int isinst, const std::vector<std::string> &list) {
					(isinst ? parseFuncInst : parseSectionInside)(parseinfo, cmd, list);
				});
		}
		else {
			parseinfo.putErrorLine();
		}
	}

	void parseFile(ParseInfo &parseinfo, PriLib::TextFile &file)
	{
		while (!file.eof()) {
			++parseinfo.lcount;

			std::string line = file.getline();

			//PriLib::Output::println("\n(", parseinfo.lcount, ") ", line);

			// Remove Comment
			size_t comment_post = line.find(';');
			if (comment_post != std::string::npos) {
				line = line.substr(0, comment_post);
			}
			if (line.empty())
				continue;

			// Parse Line
			parseLine(parseinfo, line);
		}
	}

	FunctionSet createFunctionSet(ParseInfo &parseinfo) {
		FunctionSet fset;
		for (auto &val : parseinfo.functable) {
			fset[val.first] = new InstStruct::Function(std::move(*parseinfo.functable.at(val.first)));
		}
		return fset;
	}

	std::string getEntry(ParseInfo &parseinfo) {
		return parseinfo.entry.data;
	}
}

#include "inststruct/instdef.h"

namespace CVM
{
	InstStruct::Instruction* parseFuncInstBase(ParseInfo& parseinfo, const std::string &code, const std::vector<std::string> &list)
	{
		using ParseInstProcess = std::function<InstStruct::Instruction*(ParseInfo &, const std::vector<std::string> &)>;
		using ParseInstMap = std::map<std::string, ParseInstProcess>;

		using namespace InstStruct;

		static ParseInstMap parsemap {
			{
				"mov",
				[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
					return new Insts::Move(parseRegister(parseinfo, list[0]), parseRegister(parseinfo, list[1]));
				}
			},
			{
				"load",
				[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
					return new Insts::Load1(
						parseRegister(parseinfo, list[0]),
						parseDataInst(parseinfo, list[1]),
						parseType(parseinfo, list[2]));
				}
			},
			{
				"ret",
				[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
					return new Insts::Return();
				}
			},
			{
				"db_opreg",
				[](ParseInfo &parseinfo, const std::vector<std::string> &list) {
					return new Insts::Debug_OutputRegister();
				}
			},
		};

		auto iter = parsemap.find(code);
		if (iter != parsemap.end()) {
			return iter->second(parseinfo, list);
		}
		else {
			parseinfo.putErrorLine(PEC_URIns, code);
			return nullptr;
		}
	}
}
