#include "obj.inl.hpp"

#include <filesystem>
#include <vector>
#include <string_view>
#include <stdexcept>

#include <fmt/format.h>



namespace fmamdl::conv {

	using StringViews = std::vector<std::string_view>;

	posixfio::OutputBuffer stdoutBuf = posixfio::OutputBuffer(posixfio::FileView(1), 4096);
	posixfio::OutputBuffer stderrBuf = posixfio::OutputBuffer(posixfio::FileView(1), 4096);

	std::string_view usageStr =
		"<source> <destination> [options...]\n"
		"\t-t <file>, --texture-prefix <file>\n"
		"\t-M,        --no-material-output\n"
		"\t-b <bone>, --main-bone <bone>\n";


	std::filesystem::path parseArg0(std::string_view arg0) {
		std::filesystem::path p = arg0;
		return p.filename();
	}


	void printUsage(std::string_view arg0) {
		auto str = fmt::format("Usage: {} {}", parseArg0(arg0).c_str(), usageStr);
		stdoutBuf.writeAll(str.data(), str.size());
	}


	enum class OptionType {
		eInvalid,
		eArgument,
		eTexturePrefix,
		eNoMaterials,
		eOnlyMaterials,
		eMainBone,
		eLiteral,
		eDash
	};

	struct ParsedOption {
		std::string_view value;
		std::string_view next;
		OptionType type;
		bool       consumeNext;
	};


	ParsedOption optionFsm(const std::string_view& value, const std::string_view& next) {
		constexpr char nul = '\0';
		const char*    c   = value.begin();

		if(*c == '-') { ++c; goto check_if_long_opt; }
		return { value, next, OptionType::eArgument, false };

		check_if_long_opt:
		if(*c == '-') { ++c; goto long_opt_beg; }
		if(*c == nul) return { value, next, OptionType::eDash, false };
		if(*c == 'b') { ++c; goto short_b; }
		if(*c == 'm') { ++c; goto short_m; }
		if(*c == 'M') { ++c; goto short_M; }
		if(*c == 't') { ++c; goto short_t; }
		goto error;

		short_b:
		if(*c == nul) return { "-b", next, OptionType::eMainBone, true };
		return { "-b", std::string_view(value.data() + 2, value.size() - 2), OptionType::eMainBone, false };

		short_m:
		if(*c == nul) return { "-m", next, OptionType::eOnlyMaterials, false };
		return { "-m", std::string_view(value.data() + 2, value.size() - 2), OptionType::eOnlyMaterials, false };

		short_M:
		if(*c == nul) return { "-M", next, OptionType::eNoMaterials, false };
		return { "-M", std::string_view(value.data() + 2, value.size() - 2), OptionType::eNoMaterials, false };

		short_t:
		if(*c == nul) return { "-t", next, OptionType::eTexturePrefix, true };
		return { "-t", std::string_view(value.data() + 2, value.size() - 2), OptionType::eTexturePrefix, false };

		long_opt_beg:
		if(*c == nul) return { value, next, OptionType::eLiteral, false };
		if(*c == 'm') goto long_m;
		if(*c == 'n') goto long_n;
		if(*c == 'o') goto long_o;
		if(*c == 't') goto long_t;
		goto error;

		long_m:        if(*c == 'a') { ++c; goto long_ma; }
		long_ma:       if(*c == 'i') { ++c; goto long_mai; }
		long_mai:      if(*c == 'n') { ++c; goto long_main; }
		long_main:     if(*c == '-') { ++c; goto long_main_; }
		long_main_:    if(*c == 'b') { ++c; goto long_main_b; }
		long_main_b:   if(*c == 'o') { ++c; goto long_main_bo; }
		long_main_bo:  if(*c == 'n') { ++c; goto long_main_bon; }
		long_main_bon: if(*c == 'e') { ++c; goto long_main_bone; }
		long_main_bone:
		if(*c == nul) return { "-b", next, OptionType::eMainBone, true };
		goto error;

		long_n:                  if(*c == 'o') { ++c; goto long_no; }
		long_no:                 if(*c == '-') { ++c; goto long_no_; }
		long_no_:                if(*c == 'm') { ++c; goto long_no_m; }
		long_no_m:               if(*c == 'a') { ++c; goto long_no_ma; }
		long_no_ma:              if(*c == 't') { ++c; goto long_no_mat; }
		long_no_mat:             if(*c == 'e') { ++c; goto long_no_mate; }
		long_no_mate:            if(*c == 'r') { ++c; goto long_no_mater; }
		long_no_mater:           if(*c == 'i') { ++c; goto long_no_materi; }
		long_no_materi:          if(*c == 'a') { ++c; goto long_no_materia; }
		long_no_materia:         if(*c == 'l') { ++c; goto long_no_material; }
		long_no_material:        if(*c == 's') { ++c; goto long_no_materials; }
		long_no_materials:
		if(*c == nul) return { "-M", next, OptionType::eNoMaterials, false };
		goto error;

		long_o:                    if(*c == 'n') { ++c; goto long_on; }
		long_on:                   if(*c == 'l') { ++c; goto long_onl; }
		long_onl:                  if(*c == 'y') { ++c; goto long_only; }
		long_only:                 if(*c == '-') { ++c; goto long_only_; }
		long_only_:                if(*c == 'm') { ++c; goto long_only_m; }
		long_only_m:               if(*c == 'a') { ++c; goto long_only_ma; }
		long_only_ma:              if(*c == 't') { ++c; goto long_only_mat; }
		long_only_mat:             if(*c == 'e') { ++c; goto long_only_mate; }
		long_only_mate:            if(*c == 'r') { ++c; goto long_only_mater; }
		long_only_mater:           if(*c == 'i') { ++c; goto long_only_materi; }
		long_only_materi:          if(*c == 'a') { ++c; goto long_only_materia; }
		long_only_materia:         if(*c == 'l') { ++c; goto long_only_material; }
		long_only_material:        if(*c == 's') { ++c; goto long_only_materials; }
		long_only_materials:
		if(*c == nul) return { "-n", next, OptionType::eOnlyMaterials, false };
		goto error;

		long_t:             if(*c == 'e') { ++c; goto long_te; }
		long_te:            if(*c == 'x') { ++c; goto long_tex; }
		long_tex:           if(*c == 't') { ++c; goto long_text; }
		long_text:          if(*c == 'u') { ++c; goto long_textu; }
		long_textu:         if(*c == 'r') { ++c; goto long_textur; }
		long_textur:        if(*c == 'e') { ++c; goto long_texture; }
		long_texture:       if(*c == '-') { ++c; goto long_texture_; }
		long_texture_:      if(*c == 'p') { ++c; goto long_texture_p; }
		long_texture_p:     if(*c == 'r') { ++c; goto long_texture_pr; }
		long_texture_pr:    if(*c == 'e') { ++c; goto long_texture_pre; }
		long_texture_pre:   if(*c == 'f') { ++c; goto long_texture_pref; }
		long_texture_pref:  if(*c == 'i') { ++c; goto long_texture_prefi; }
		long_texture_prefi: if(*c == 'x') { ++c; goto long_texture_prefix; }
		long_texture_prefix:
		if(*c == nul) return { "-t", next, OptionType::eTexturePrefix, true };
		goto error;

		error:
		return { value, next, OptionType::eInvalid, false };
	}


	Options parseOptions(const StringViews& args) {
		Options r;
		r.mainBone = std::string_view("\033first");
		bool srcGiven = false;
		bool dstGiven = false;
		bool literal  = false;

		auto iter0 = args.begin();
		auto iter1 = iter0 + 1;
		while(iter0 != args.end()) {
			bool iter1eof = iter1 == args.end();
			auto str1     = iter1eof? std::string_view { } : *iter1;
			ParsedOption opt;
			if(literal) opt = { *iter0, str1, OptionType::eArgument, false };
			else        opt = optionFsm(*iter0, str1);

			switch(opt.type) {
				case OptionType::eInvalid: throw std::invalid_argument(
					"Invalid option \"" + std::string(opt.value) + "\"" );
				case OptionType::eDash:
					opt.value = std::string_view("\033stdio", 6);
					[[fallthrough]];
				case OptionType::eArgument:
					if(! srcGiven) {
						r.srcName = opt.value;
						srcGiven = true;
					} else
					if(! dstGiven) {
						r.dstName = opt.value;
						dstGiven = true;
					} else {
						throw std::invalid_argument("Too many arguments");
					}
					break;
				case OptionType::eLiteral:
					literal = true;
					break;
				case OptionType::eNoMaterials:
					r.noMaterials = true;
					break;
				case OptionType::eOnlyMaterials:
					r.onlyMaterials = true;
					break;
				case OptionType::eTexturePrefix:
					r.texturePrefix = opt.next;
					break;
				case OptionType::eMainBone:
					r.mainBone = opt.next;
					break;
			}
			if(opt.consumeNext) {
				if(! iter1eof) ++ iter1;
			};
			iter0 = iter1;
			++ iter1;
		}

		return r;
	}

}



int main(int argn, char** argv) {
	using namespace fmamdl::conv;

	auto& eb = fmamdl::conv::stderrBuf;
	int   r  = EXIT_FAILURE;

	auto args = std::vector<std::string_view>();
	args.reserve(argn-1);
	for(int i = 1; i < argn; ++i) args.push_back(argv[i]);

	if(argn < 3) {
		stderrBuf.writeAll("Missing required arguments.\n", 28);
		printUsage(argv[0]);
		return EXIT_FAILURE;
	}

	try {
		fmamdl::conv::Options opt = parseOptions(args);
		fmamdl::conv::obj::convert(opt);
		r = EXIT_SUCCESS;
	} catch(std::exception& ex) {
		auto  n = fmt::format("Error: {}\n", ex.what());
		eb.writeAll(n.data(), n.length());
	} catch(posixfio::Errcode& er) {
		auto  n = fmt::format("File error: errno {}\n", er.errcode);
		eb.writeAll(n.data(), n.length());
	}

	return r;
}
