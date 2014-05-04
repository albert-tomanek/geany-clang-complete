/*
 * completion.cpp - a Geany plugin to provide code completion using clang
 *
 * Copyright (C) 2014 Noto, Yuta <nonotetau(at)gmail(dot)com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "completion.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cstring>

#include <clang-c/Index.h>

using namespace cc;

static CompleteResultType getCursorType(const CXCompletionResult& result)
{
	switch(result.CursorKind) {
	case CXCursor_StructDecl: case CXCursor_UnionDecl: case CXCursor_ClassDecl:
	case CXCursor_TypedefDecl: case CXCursor_ClassTemplate: case CXCursor_Constructor:
		return COMPLETE_RESULT_CLASS;
	case CXCursor_CXXMethod: case CXCursor_Destructor:
		return COMPLETE_RESULT_METHOD;
	case CXCursor_FunctionDecl : case CXCursor_FunctionTemplate:
		return COMPLETE_RESULT_FUNCTION;
	case CXCursor_VarDecl: case CXCursor_EnumConstantDecl:
		return COMPLETE_RESULT_VAR;
	case CXCursor_FieldDecl:
		return COMPLETE_RESULT_MEMBER;
	case CXCursor_Namespace:
		return COMPLETE_RESULT_NAMESPACE;
	case CXCursor_MacroDefinition:
		//return COMPLETE_RESULT_MACRO;
		return COMPLETE_RESULT_NONE;
	case CXCursor_EnumDecl:
		return COMPLETE_RESULT_OTHER;
	case CXCursor_NotImplemented: //keywords?
	case CXCursor_ParmDecl: //function arg?
		return COMPLETE_RESULT_NONE;
	default:
		std::cout<<"unknown chunk"<<  result.CursorKind <<std::endl;
		return COMPLETE_RESULT_NONE;
	}
}
enum CompleteResultAvailability {
	COMPLETE_RESULT_AVAIL_AVAIL,
	COMPLETE_RESULT_AVAIL_DEPRECATED,
	COMPLETE_RESULT_AVAIL_NOTAVAIL,
	COMPLETE_RESULT_AVAIL_NOTACCESS
};
struct ParseResult {
	std::string return_type;
	std::string typed_text;
	std::string signature;
	CompleteResultAvailability availability;
	CompleteResultType type;
};

struct CompletionStringParser {
	ParseResult* r;
	void append(std::string& stdstr, CXCompletionString comp_str, unsigned chunk_idx) {
		CXString text = clang_getCompletionChunkText(comp_str, chunk_idx);
		const char* cstr = clang_getCString(text);
		stdstr += cstr ? cstr : "";
		clang_disposeString(text);
	}
	void setAveailability(CXCompletionString comp_str) {
		CXAvailabilityKind kind = clang_getCompletionAvailability(comp_str);
		switch(kind) {
			case CXAvailability_Available:
				r->availability = COMPLETE_RESULT_AVAIL_AVAIL;       break;
			case CXAvailability_Deprecated:
				r->availability = COMPLETE_RESULT_AVAIL_DEPRECATED; break;
			case CXAvailability_NotAvailable:
				r->availability = COMPLETE_RESULT_AVAIL_NOTAVAIL;    break;
			case CXAvailability_NotAccessible:
				r->availability = COMPLETE_RESULT_AVAIL_NOTACCESS;   break;
		}
	}

	int enter_optional_count;

	void do_parse(CXCompletionString comp_str)
	{
		int N = clang_getNumCompletionChunks(comp_str);
		for(int i = 0; i < N; i++)
		{
			CXCompletionChunkKind kind = clang_getCompletionChunkKind(comp_str, i);

			switch(kind) {
			case CXCompletionChunk_Optional:
				if( enter_optional_count == 0 ) { r->signature += "{"; }
				enter_optional_count += 1;
				do_parse(clang_getCompletionChunkCompletionString(comp_str, i));
				enter_optional_count -= 1;
				if( enter_optional_count == 0 ) { r->signature += "}"; }
				break;
			case CXCompletionChunk_TypedText:
				append(r->typed_text, comp_str, i);
				r->signature += r->typed_text;
				break;
			case CXCompletionChunk_ResultType:
				append(r->return_type, comp_str, i);
				break;
			case CXCompletionChunk_Placeholder:
				append(r->signature, comp_str, i);
				r->signature += "{PH}";
				break;
			case CXCompletionChunk_Text:
				append(r->signature, comp_str, i);
				//r->signature += "{TX}";
				break;
			case CXCompletionChunk_Informative:
				append(r->signature, comp_str, i);
				//r->signature += "{IF}";
				break;
			case CXCompletionChunk_CurrentParameter:
				append(r->signature, comp_str, i);
				//r->signature += "{CP}";
				break;
			//start & end show func params?
			case CXCompletionChunk_LeftParen:        r->signature += '('; break;
			case CXCompletionChunk_RightParen:       r->signature += ')'; break;

			case CXCompletionChunk_LeftBracket:      r->signature += '['; break;
			case CXCompletionChunk_RightBracket:     r->signature += ']'; break;
			case CXCompletionChunk_LeftBrace:        r->signature += '{'; break;
			case CXCompletionChunk_RightBrace:       r->signature += '}'; break;
			case CXCompletionChunk_LeftAngle:        r->signature += '<'; break;
			case CXCompletionChunk_RightAngle:       r->signature += '>'; break;
			case CXCompletionChunk_Comma:            r->signature += ", "; break;
			case CXCompletionChunk_Colon:            r->signature += ':'; break;
			case CXCompletionChunk_SemiColon:        r->signature += ';'; break;
			case CXCompletionChunk_Equal:            r->signature += '='; break;
			case CXCompletionChunk_HorizontalSpace:  r->signature += ' '; break;
			case CXCompletionChunk_VerticalSpace:    r->signature += "\n"; break;
			}
		}
	}

	void parse(ParseResult* r, const CXCompletionResult& result) {
		this->r = r;
		CXCompletionString comp_str = result.CompletionString;
		setAveailability(comp_str);
		r->type = getCursorType(result);
		enter_optional_count = 0;
		do_parse(comp_str);
		if( r->return_type != "" ) {
			r->signature += " -> ";
			r->signature += r->return_type;
		}
		if( r->availability == COMPLETE_RESULT_AVAIL_NOTACCESS ) {
			r->signature.insert(0, "(NOTACCESS) ");
		}
		//unsigned priority = clang_getCompletionPriority(comp_str);
		//char buf[1024] = {0};
		//sprintf(buf, "%u", priority);
		//r->signature += buf;
	}
};

class CodeCompletion::CodeCompletionImpl {
public:
	CXIndex index;
	std::map<std::string, CXTranslationUnit> tuCache;
	std::vector<std::string> commandLineArgs;

	CodeCompletionImpl() {
		index = clang_createIndex(0, 0); /* (excludeDeclarationsFromPCH, displayDiagnostics) */
		if(!index) {
			std::cerr<< "an unexpected error @ clang_createIndex" <<std::endl;
		}
		std::cout<<"created codecomplete"<<std::endl;
	}
	~CodeCompletionImpl() {
		clearTranslationUnitCache();
		clang_disposeIndex(index);
		std::cout<<"destroyed codecomplete"<<std::endl;
	}

	void clearTranslationUnitCache() {
		std::map<std::string, CXTranslationUnit>::iterator it = tuCache.begin();
		while( it != tuCache.end() ) {
			if( it->second ) {
				clang_disposeTranslationUnit(it->second);
			}
			++it;
		}
		tuCache.clear();
	}

	CXTranslationUnit getTranslationUnit(const char* filename, const char* content) {
		CXTranslationUnit tu;
		if( tuCache.find(filename) == tuCache.end() ) {//not found -> create
			const char** argv = new const char*[commandLineArgs.size()];
			for(size_t i=0; i<commandLineArgs.size(); i++) {
				argv[i] = commandLineArgs[i].c_str();
			}

			CXUnsavedFile f[1];
			f[0].Filename = filename; f[0].Contents = content; f[0].Length = strlen(content);

			//flag on 0x04 0x08 ? (precompile-preamble & caching complete result)
			tu = clang_parseTranslationUnit(index, filename, argv, commandLineArgs.size(),
											f, 1, clang_defaultEditingTranslationUnitOptions() );
			if(tu) {
				tuCache.insert(std::pair<std::string, CXTranslationUnit>(filename, tu));
			}
			delete [] argv;
		} else {
			tu = tuCache[filename];
		}
		return tu;
	}
	void setOption(std::vector<std::string>& options) {
		commandLineArgs.clear();
		for(size_t i=0; i<options.size(); i++) {
			commandLineArgs.push_back(options[i]);
		}
		clearTranslationUnitCache();
	}
	void complete(CodeCompletionResults& result,
		const char* filename, const char* content, int line, int col, int flag) {

		if(index == NULL) { return; }
		result.clear();

		CXTranslationUnit tu = getTranslationUnit(filename, content);
		if(!tu) {
			std::cerr<< "an unexpected error @ clang_parseTranslationUnit" <<std::endl;
			return;
		}
		CXUnsavedFile f[1];
		f[0].Filename = filename; f[0].Contents = content; f[0].Length = strlen(content);

		/* NEED reparse! */
		clang_reparseTranslationUnit(tu, 1, f, clang_defaultReparseOptions(tu));

		// clang_defaultCodeCompleteOptions() (include macro)
		unsigned comp_flag = clang_defaultCodeCompleteOptions();
		CXCodeCompleteResults* results =
			clang_codeCompleteAt(tu, filename, line, col, f, 1, comp_flag );

		if(!results) {
			std::cerr<< "an unexpected error @ clang_codeCompleteAt" <<std::endl;
			return;
		}

		//some info
		{
			unsigned diag_num = clang_codeCompleteGetNumDiagnostics(results);
			for(unsigned i = 0; i < diag_num; i++) {
				CXDiagnostic diag = clang_codeCompleteGetDiagnostic(results, i);
				CXString text = clang_formatDiagnostic(diag,
					clang_defaultDiagnosticDisplayOptions());
				const char* str = clang_getCString(text);
				std::cerr<< "diag "<<i<<" "<< str <<std::endl;
				clang_disposeDiagnostic(diag);
				clang_disposeString(text);
			}

			long long cc_context = clang_codeCompleteGetContexts(results);
			std::cerr<< "cc_context "<< cc_context <<std::endl;

			// container such as class, struct, ...
			//unsigned IsIncomplete;
			//CXCursorKind ckind = clang_codeCompleteGetContainerKind(results, &IsIncomplete);
			//std::cerr<< "cursor "<< ckind << " iscompleted="<<IsIncomplete<<std::endl;
			//CXString cxstr = clang_codeCompleteGetContainerUSR(results);
			//const char* str = clang_getCString(cxstr);
			//std::cerr<< "usr "<< str <<std::endl;
			//clang_disposeString(cxstr);
		}

		if( results->NumResults == 0 ) {
			std::cerr<< "no code completion!!!" <<std::endl;
			return;
		} else {
			clang_sortCodeCompletionResults(results->Results, results->NumResults);
			for(int i=0; i<results->NumResults; i++) {
				CompleteResultRow r;
				r.type = getCursorType(results->Results[i]);

				if(r.type != COMPLETE_RESULT_NONE) {
					ParseResult pr;
					CompletionStringParser().parse(&pr, results->Results[i]);
					r.typedText = pr.typed_text;
					r.label = pr.signature;
					//getExpression(results->Results[i].CompletionString, r.typedText, r.label);
					result.push_back(r);
				}

				{ // for annotate
					CXCompletionString comp_str = results->Results[i].CompletionString;

					unsigned anno = clang_getCompletionNumAnnotations(comp_str);
					for(unsigned i=0; i<anno; i++) {
						CXString text = clang_getCompletionAnnotation(comp_str, i);
						printf("anno %s\n", clang_getCString(text) );
						clang_disposeString(text);
					}
				}

			}
		}
		clang_disposeCodeCompleteResults(results);
	}

	//int completeAsync(const char* filename, const char* content, int line, int col);
private:
	CodeCompletionImpl(const CodeCompletionImpl&);
	void operator=(const CodeCompletionImpl&);
};


//CodeCompletion////////////////////////////////////////////////////////////////
CodeCompletion::CodeCompletion() : pimpl(new CodeCompletionImpl()) {}
CodeCompletion::~CodeCompletion() {delete pimpl;}

void CodeCompletion::setOption(std::vector<std::string>& options) {
	pimpl->setOption(options);
}
void CodeCompletion::complete(CodeCompletionResults& result,
	const char* filename, const char* content, int line, int col, int flag) {

	pimpl->complete(result, filename, content, line, col, flag);
}

int CodeCompletion::completeAsync(const char* filename, const char* content, int line, int col) {
	return 0;
}

