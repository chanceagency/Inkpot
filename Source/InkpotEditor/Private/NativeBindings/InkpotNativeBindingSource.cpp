#include "NativeBindings/InkpotNativeBindingSource.h"
#include "Prosetta/ProsettaChecksum.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* SupportedParameterTypes[] = { TEXT("int"), TEXT("float"), TEXT("bool"), TEXT("const char*") };
	const TCHAR* SupportedReturnTypes[] = { TEXT("int"), TEXT("float"), TEXT("bool"), TEXT("const char*"), TEXT("void") };

	bool IsSupported(const FString& InType, const TCHAR* const* InTypes, int32 InCount)
	{
		for (int32 index = 0; index < InCount; ++index)
			if (InType == InTypes[index])
				return true;
		return false;
	}

	bool IsIdentifierStart(TCHAR C) { return FChar::IsAlpha(C) || C == TEXT('_'); }
	bool IsIdentifierChar(TCHAR C) { return FChar::IsAlnum(C) || C == TEXT('_'); }

	int32 LineAtOffset(const FString& InText, int32 InOffset)
	{
		int32 line = 1;
		const int32 end = FMath::Clamp(InOffset, 0, InText.Len());
		for (int32 index = 0; index < end; ++index)
			if (InText[index] == TEXT('\n'))
				++line;
		return line;
	}

	FString NormalizeType(const FString& InRaw)
	{
		FString type = InRaw;
		type.TrimStartAndEndInline();
		// Collapse whitespace, then tighten pointer / reference spacing: `const char *` -> `const char*`.
		FString collapsed;
		bool bLastSpace = false;
		for (TCHAR c : type)
		{
			if (FChar::IsWhitespace(c))
			{
				if (!bLastSpace) collapsed.AppendChar(TEXT(' '));
				bLastSpace = true;
			}
			else
			{
				collapsed.AppendChar(c);
				bLastSpace = false;
			}
		}
		collapsed.ReplaceInline(TEXT(" *"), TEXT("*"));
		collapsed.ReplaceInline(TEXT("* "), TEXT("*"));
		collapsed.ReplaceInline(TEXT(" &"), TEXT("&"));
		collapsed.ReplaceInline(TEXT("& "), TEXT("&"));
		collapsed.TrimStartAndEndInline();
		return collapsed;
	}

	/** Replaces the contents of every brace block with spaces ( newlines kept ) so only the outermost declarations remain. */
	FString BlankNestedBlocks(const FString& InBody)
	{
		FString output;
		output.Reserve(InBody.Len());
		int32 depth = 0;
		for (TCHAR c : InBody)
		{
			if (c == TEXT('{'))
			{
				output.AppendChar(c);
				++depth;
			}
			else if (c == TEXT('}'))
			{
				--depth;
				output.AppendChar(c);
			}
			else
			{
				output.AppendChar(depth > 0 && c != TEXT('\n') ? TEXT(' ') : c);
			}
		}
		return output;
	}

	/** Finds the body of the block whose opening brace is at or after InFrom. Returns false when unbalanced. */
	bool FindBlockBody(const FString& InCode, int32 InFrom, int32& OutOpen, int32& OutClose)
	{
		OutOpen = InCode.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, InFrom);
		if (OutOpen == INDEX_NONE)
			return false;
		int32 depth = 0;
		for (int32 index = OutOpen; index < InCode.Len(); ++index)
		{
			if (InCode[index] == TEXT('{')) ++depth;
			else if (InCode[index] == TEXT('}'))
			{
				--depth;
				if (depth == 0)
				{
					OutClose = index;
					return true;
				}
			}
		}
		return false;
	}

	void AddDiagnostic(FInkpotNativeBindingSourceModule& InModule, const TCHAR* InSeverity, const TCHAR* InCode, const FString& InMessage, int32 InLine = 0)
	{
		FInkpotNativeBindingSourceDiagnostic diagnostic;
		diagnostic.Severity = InSeverity;
		diagnostic.Code = InCode;
		diagnostic.Message = InMessage;
		diagnostic.Line = InLine;
		InModule.Diagnostics.Add(MoveTemp(diagnostic));
	}

	TArray<FString> SplitParameterList(const FString& InRaw)
	{
		TArray<FString> parameters;
		int32 depth = 0;
		FString current;
		for (TCHAR c : InRaw)
		{
			if (c == TEXT('<') || c == TEXT('(') || c == TEXT('[')) ++depth;
			else if (c == TEXT('>') || c == TEXT(')') || c == TEXT(']')) --depth;
			if (c == TEXT(',') && depth == 0)
			{
				parameters.Add(current.TrimStartAndEnd());
				current.Reset();
			}
			else
			{
				current.AppendChar(c);
			}
		}
		if (!current.TrimStartAndEnd().IsEmpty())
			parameters.Add(current.TrimStartAndEnd());
		return parameters;
	}

	/** True when the original line above the signature is a comment carrying @lookaheadSafe. */
	bool HasLookaheadSafeAnnotation(const TArray<FString>& InLines, int32 InSignatureLine)
	{
		// Walk up over at most three preceding lines; stop at the first line that is not a comment.
		for (int32 line = InSignatureLine - 1, checked = 0; line >= 1 && checked < 3; --line, ++checked)
		{
			const FString text = InLines[line - 1].TrimStartAndEnd();
			if (text.IsEmpty())
				return false;
			const bool bComment = text.StartsWith(TEXT("//")) || text.StartsWith(TEXT("/*")) || text.StartsWith(TEXT("*"));
			if (!bComment)
				return false;
			if (text.Contains(TEXT("@lookaheadSafe"), ESearchCase::CaseSensitive))
				return true;
		}
		return false;
	}

	bool IsStubBody(const FString& InStrippedBody)
	{
		FString body = InStrippedBody;
		body.TrimStartAndEndInline();
		if (body.IsEmpty())
			return true;
		if (!body.StartsWith(TEXT("return")) || !body.EndsWith(TEXT(";")))
			return false;
		FString value = body.Mid(6, body.Len() - 7).TrimStartAndEnd();
		static const TCHAR* Zeros[] = { TEXT("0"), TEXT("0.0"), TEXT("0.0f"), TEXT("0.f"), TEXT("0f"), TEXT("false"), TEXT("\"\"") };
		for (const TCHAR* zero : Zeros)
			if (value == zero)
				return true;
		return false;
	}
}

FString FInkpotNativeBindingSourceFunction::Signature() const
{
	TArray<FString> parameters;
	for (const FInkpotNativeBindingSourceParameter& parameter : Parameters)
		parameters.Add(parameter.Name.IsEmpty() ? parameter.Type : parameter.Type + TEXT(" ") + parameter.Name);
	return FString::Printf(TEXT("%s %s(%s)"), *ReturnType, *Name, *FString::Join(parameters, TEXT(", ")));
}

FString FInkpotNativeBindingSourceDiagnostic::ToString() const
{
	return Line > 0 ? FString::Printf(TEXT("%s %s ( line %d ): %s"), *Severity, *Code, Line, *Message) : FString::Printf(TEXT("%s %s: %s"), *Severity, *Code, *Message);
}

bool FInkpotNativeBindingSourceModule::HasErrors() const
{
	return ErrorCount() > 0;
}

int32 FInkpotNativeBindingSourceModule::ErrorCount() const
{
	int32 count = 0;
	for (const FInkpotNativeBindingSourceDiagnostic& diagnostic : Diagnostics)
		if (diagnostic.IsError()) ++count;
	return count;
}

int32 FInkpotNativeBindingSourceModule::WarningCount() const
{
	return Diagnostics.Num() - ErrorCount();
}

const FInkpotNativeBindingSourceFunction* FInkpotNativeBindingSourceModule::FindFunction(const FString& InName) const
{
	return Functions.FindByPredicate([&InName](const FInkpotNativeBindingSourceFunction& F) { return F.Name == InName; });
}

bool FInkpotNativeBindingSourceScanner::IsValidModuleName(const FString& InName)
{
	if (InName.IsEmpty() || InName.Len() > 64 || !IsIdentifierStart(InName[0]))
		return false;
	for (TCHAR c : InName)
		if (!IsIdentifierChar(c))
			return false;
	return true;
}

FString FInkpotNativeBindingSourceScanner::Sha256(const FString& InText)
{
	FTCHARToUTF8 utf8(*InText, InText.Len());
	return ProsettaChecksum::SHA256(utf8.Get(), utf8.Length()).ToLower();
}

FString FInkpotNativeBindingSourceScanner::StripCommentsAndStrings(const FString& InContent)
{
	FString output;
	output.Reserve(InContent.Len());
	const int32 length = InContent.Len();
	enum class EState { Code, LineComment, BlockComment, String, Char };
	EState state = EState::Code;
	for (int32 index = 0; index < length; ++index)
	{
		const TCHAR c = InContent[index];
		const TCHAR next = index + 1 < length ? InContent[index + 1] : TEXT('\0');
		switch (state)
		{
		case EState::Code:
			if (c == TEXT('/') && next == TEXT('/')) { state = EState::LineComment; output.Append(TEXT("  ")); ++index; }
			else if (c == TEXT('/') && next == TEXT('*')) { state = EState::BlockComment; output.Append(TEXT("  ")); ++index; }
			else if (c == TEXT('"')) { state = EState::String; output.AppendChar(c); }
			else if (c == TEXT('\'')) { state = EState::Char; output.AppendChar(c); }
			else output.AppendChar(c);
			break;
		case EState::LineComment:
			if (c == TEXT('\n')) { state = EState::Code; output.AppendChar(c); }
			else output.AppendChar(TEXT(' '));
			break;
		case EState::BlockComment:
			if (c == TEXT('*') && next == TEXT('/')) { state = EState::Code; output.Append(TEXT("  ")); ++index; }
			else output.AppendChar(c == TEXT('\n') ? c : TEXT(' '));
			break;
		case EState::String:
		case EState::Char:
			if (c == TEXT('\\') && index + 1 < length) { output.Append(TEXT("  ")); ++index; }
			else if ((state == EState::String && c == TEXT('"')) || (state == EState::Char && c == TEXT('\''))) { state = EState::Code; output.AppendChar(c); }
			else output.AppendChar(c == TEXT('\n') ? c : TEXT(' '));
			break;
		}
	}
	return output;
}

bool FInkpotNativeBindingSourceScanner::ParseProvenance(const FString& InContent, FInkpotNativeBindingProvenance& OutProvenance, int32& OutAuthoredOffset)
{
	OutProvenance = FInkpotNativeBindingProvenance();
	OutAuthoredOffset = 0;

	// Header: three `//` lines then one blank line; each line ends in \n ( \r\n tolerated ).
	TArray<FString> lines;
	int32 cursor = 0;
	for (int32 lineIndex = 0; lineIndex < 4; ++lineIndex)
	{
		int32 newline = InContent.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, cursor);
		if (newline == INDEX_NONE)
			return false;
		FString line = InContent.Mid(cursor, newline - cursor);
		line.RemoveFromEnd(TEXT("\r"));
		lines.Add(line);
		cursor = newline + 1;
	}
	if (!lines[0].StartsWith(TEXT("// Generated by Prosetta")) || !lines[3].TrimStartAndEnd().IsEmpty())
		return false;

	const FRegexPattern identity(TEXT("^//\\s*prosetta-native-binding\\s+module=([A-Za-z0-9_]+)\\s+language=([a-z]+)\\s+project=(\\d+)\\s+scriptId=(\\d+)\\s*$"));
	FRegexMatcher identityMatcher(identity, lines[1]);
	if (!identityMatcher.FindNext())
		return false;
	const FRegexPattern sha(TEXT("^//\\s*prosetta-source-sha256=([0-9a-fA-F]{64})\\s*$"));
	FRegexMatcher shaMatcher(sha, lines[2]);
	if (!shaMatcher.FindNext())
		return false;

	OutProvenance.bPresent = true;
	OutProvenance.ModuleName = identityMatcher.GetCaptureGroup(1);
	OutProvenance.Language = identityMatcher.GetCaptureGroup(2);
	OutProvenance.ProjectId = FCString::Atoi(*identityMatcher.GetCaptureGroup(3));
	OutProvenance.ScriptId = FCString::Atoi(*identityMatcher.GetCaptureGroup(4));
	OutProvenance.SourceSha256 = shaMatcher.GetCaptureGroup(1).ToLower();
	OutAuthoredOffset = cursor;
	return true;
}

bool FInkpotNativeBindingSourceScanner::LooksLikeModule(const FString& InContent)
{
	return InContent.Contains(TEXT("namespace Prosetta::Bindings::"), ESearchCase::CaseSensitive);
}

bool FInkpotNativeBindingSourceScanner::ScanFile(const FString& InFilename, FInkpotNativeBindingSourceModule& OutModule, bool bUseFilenameAsModuleName)
{
	FString content;
	OutModule = FInkpotNativeBindingSourceModule();
	OutModule.Filename = InFilename;
	if (!FFileHelper::LoadFileToString(content, *InFilename))
	{
		AddDiagnostic(OutModule, TEXT("error"), TEXT("file-unreadable"), FString::Printf(TEXT("Cannot read '%s'."), *InFilename));
		return false;
	}
	OutModule = Scan(content, bUseFilenameAsModuleName ? FPaths::GetBaseFilename(InFilename) : FString());
	OutModule.Filename = InFilename;
	return OutModule.IsValid();
}

FInkpotNativeBindingSourceModule FInkpotNativeBindingSourceScanner::Scan(const FString& InContent, const FString& InExpectedModuleName)
{
	FInkpotNativeBindingSourceModule module;

	int32 authoredOffset = 0;
	if (ParseProvenance(InContent, module.Provenance, authoredOffset))
	{
		module.AuthoredSha256 = Sha256(InContent.Mid(authoredOffset));
		if (!module.AuthoredSha256.Equals(module.Provenance.SourceSha256, ESearchCase::IgnoreCase))
			AddDiagnostic(module, TEXT("error"), TEXT("provenance-sha-mismatch"), FString::Printf(TEXT("The authored source hashes to %s but the Prosetta header records %s; the file was edited after sync."), *module.AuthoredSha256.Left(12), *module.Provenance.SourceSha256.Left(12)));
		if (module.Provenance.Language != TEXT("cpp"))
			AddDiagnostic(module, TEXT("error"), TEXT("provenance-language"), FString::Printf(TEXT("The Prosetta header says language=%s; only cpp modules can be loaded by Inkpot."), *module.Provenance.Language));
	}
	else
	{
		module.AuthoredSha256 = Sha256(InContent);
	}

	FString expectedModuleName = InExpectedModuleName;
	if (expectedModuleName.IsEmpty() && module.Provenance.bPresent)
		expectedModuleName = module.Provenance.ModuleName;
	if (module.Provenance.bPresent && !InExpectedModuleName.IsEmpty() && !module.Provenance.ModuleName.Equals(InExpectedModuleName, ESearchCase::IgnoreCase))
		AddDiagnostic(module, TEXT("error"), TEXT("provenance-module"), FString::Printf(TEXT("The Prosetta header names module '%s' but '%s' was expected ( file name / manifest )."), *module.Provenance.ModuleName, *InExpectedModuleName));

	const FString code = StripCommentsAndStrings(InContent);

	TArray<FString> originalLines;
	InContent.ParseIntoArrayLines(originalLines, false);

	// Includes are read from the original source: the stripper blanks the "header.h" form.
	{
		const FRegexPattern includePattern(TEXT("(?m)^[ \\t]*#\\s*include\\s*[<\"]([^>\"]+)[>\"]"));
		const FRegexPattern enginePattern(TEXT("(?i)(^|/)(Engine|CoreMinimal|Kismet|Inkpot|UnrealString|GameFramework|Runtime)\\b|\\.generated\\.h$|^Unreal"));
		FRegexMatcher matcher(includePattern, InContent);
		while (matcher.FindNext())
		{
			const FString header = matcher.GetCaptureGroup(1);
			FRegexMatcher engine(enginePattern, header);
			if (engine.FindNext())
				AddDiagnostic(module, TEXT("error"), TEXT("cpp-engine-header"), FString::Printf(TEXT("'#include \"%s\"' pulls in an engine SDK header; a native module must stay engine-agnostic."), *header), LineAtOffset(InContent, matcher.GetMatchBeginning()));
		}
	}

	// Brace balance.
	{
		int32 balance = 0;
		for (TCHAR c : code)
		{
			if (c == TEXT('{')) ++balance;
			else if (c == TEXT('}')) --balance;
		}
		if (balance != 0)
		{
			AddDiagnostic(module, TEXT("error"), TEXT("cpp-braces"), TEXT("Unbalanced braces in C++ module."));
			return module;
		}
	}

	// Namespaces: exactly one, Prosetta::Bindings::<Module>.
	int32 namespaceOffset = INDEX_NONE;
	{
		const FRegexPattern namespacePattern(TEXT("\\bnamespace\\s+([A-Za-z_][A-Za-z0-9_:]*)\\s*\\{"));
		FRegexMatcher matcher(namespacePattern, code);
		struct FFound { FString Name; int32 Offset; };
		TArray<FFound> found;
		while (matcher.FindNext())
			found.Add({ matcher.GetCaptureGroup(1), matcher.GetMatchBeginning() });

		if (expectedModuleName.IsEmpty())
		{
			for (const FFound& candidate : found)
			{
				if (candidate.Name.StartsWith(NamespacePrefix()))
				{
					expectedModuleName = candidate.Name.RightChop(FCString::Strlen(NamespacePrefix()));
					break;
				}
			}
		}
		const FString expectedNamespace = FString(NamespacePrefix()) + expectedModuleName;
		for (const FFound& candidate : found)
		{
			if (!expectedModuleName.IsEmpty() && candidate.Name == expectedNamespace && namespaceOffset == INDEX_NONE)
				namespaceOffset = candidate.Offset;
			else
				AddDiagnostic(module, TEXT("error"), TEXT("cpp-namespace"), FString::Printf(TEXT("Namespace '%s' is not allowed; declare functions inside 'namespace %s'."), *candidate.Name, *expectedNamespace), LineAtOffset(code, candidate.Offset));
		}
		if (namespaceOffset == INDEX_NONE)
		{
			AddDiagnostic(module, TEXT("error"), TEXT("cpp-namespace-missing"), expectedModuleName.IsEmpty() ? FString(TEXT("Expected 'namespace Prosetta::Bindings::<Module> { ... }'.")) : FString::Printf(TEXT("Expected 'namespace %s { ... }'."), *expectedNamespace));
			return module;
		}
		module.ModuleName = expectedModuleName;
		if (!IsValidModuleName(expectedModuleName))
			AddDiagnostic(module, TEXT("error"), TEXT("invalid-module-name"), FString::Printf(TEXT("'%s' is not a valid module name ( letters, digits, underscores; at most 64 characters )."), *expectedModuleName));
	}

	int32 open = 0, close = 0;
	if (!FindBlockBody(code, namespaceOffset, open, close))
	{
		AddDiagnostic(module, TEXT("error"), TEXT("cpp-namespace-body"), TEXT("Module namespace has no body."), LineAtOffset(code, namespaceOffset));
		return module;
	}
	const FString body = code.Mid(open + 1, close - open - 1);

	// Only a global-scope `main` is an entry point.
	{
		const FRegexPattern mainPattern(TEXT("\\bmain\\s*\\("));
		FRegexMatcher matcher(mainPattern, BlankNestedBlocks(code));
		if (matcher.FindNext())
			AddDiagnostic(module, TEXT("error"), TEXT("cpp-entry-point"), TEXT("A native module must not declare an entry point ('main')."), LineAtOffset(code, matcher.GetMatchBeginning()));
	}

	// Top-level functions of the namespace; nested blocks are blanked so lambdas and locals cannot match.
	const FString topLevel = BlankNestedBlocks(body);
	{
		const FRegexPattern functionPattern(TEXT("(?:^|[;{}\\n])\\s*(?:inline\\s+|static\\s+|constexpr\\s+|\\[\\[[^\\]]*\\]\\]\\s*)*((?:const\\s+char\\s*\\*|[A-Za-z_][A-Za-z0-9_:<>]*(?:\\s*[*&])?))(?:\\s+|(?<=[*&])\\s*)([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*(?:const\\s*)?\\{"));
		FRegexMatcher matcher(functionPattern, topLevel);
		while (matcher.FindNext())
		{
			const FString rawReturnType = NormalizeType(matcher.GetCaptureGroup(1));
			const FString name = matcher.GetCaptureGroup(2);
			const FString rawParameters = matcher.GetCaptureGroup(3);
			const int32 nameOffset = matcher.GetCaptureGroupBeginning(2);
			const int32 line = LineAtOffset(code, open + 1 + nameOffset);
			// The match ends on the body's `{`; the real body is read from the unblanked namespace body.
			const int32 bodyOpenInBody = matcher.GetMatchEnding() - 1;

			if (rawReturnType == TEXT("namespace") || rawReturnType == TEXT("struct") || rawReturnType == TEXT("class") || rawReturnType == TEXT("enum") || rawReturnType == TEXT("union"))
				continue;

			if (!IsSupported(rawReturnType, SupportedReturnTypes, UE_ARRAY_COUNT(SupportedReturnTypes)))
			{
				AddDiagnostic(module, TEXT("error"), TEXT("cpp-return-type"), FString::Printf(TEXT("'%s' returns '%s'; only int, float, bool, const char* and void are supported."), *name, *rawReturnType), line);
				continue;
			}

			FInkpotNativeBindingSourceFunction function;
			function.Name = name;
			function.ReturnType = rawReturnType;
			function.Line = line;
			bool bParametersValid = true;
			for (const FString& parameter : SplitParameterList(rawParameters))
			{
				if (parameter == TEXT("void"))
					continue;
				if (parameter == TEXT("..."))
				{
					AddDiagnostic(module, TEXT("error"), TEXT("cpp-variadic"), FString::Printf(TEXT("'%s' is variadic; Ink EXTERNAL functions have a fixed arity."), *name), line);
					bParametersValid = false;
					continue;
				}
				if (parameter.Contains(TEXT("=")))
				{
					AddDiagnostic(module, TEXT("error"), TEXT("cpp-default-parameter"), FString::Printf(TEXT("'%s' has a default parameter value; Ink EXTERNAL functions have a fixed arity."), *name), line);
					bParametersValid = false;
					continue;
				}
				// Split `type name`: the name is the trailing identifier, the type is the rest.
				int32 nameStart = parameter.Len();
				while (nameStart > 0 && IsIdentifierChar(parameter[nameStart - 1]))
					--nameStart;
				const FString parameterName = parameter.Mid(nameStart);
				const FString parameterType = NormalizeType(parameter.Left(nameStart));
				if (parameterName.IsEmpty() || !IsIdentifierStart(parameterName[0]) || parameterType.IsEmpty() || !IsSupported(parameterType, SupportedParameterTypes, UE_ARRAY_COUNT(SupportedParameterTypes)))
				{
					AddDiagnostic(module, TEXT("error"), TEXT("cpp-parameter-type"), FString::Printf(TEXT("'%s' has parameter '%s'; only int, float, bool and const char* are supported."), *name, *parameter), line);
					bParametersValid = false;
					continue;
				}
				function.Parameters.Add({ parameterType, parameterName });
			}
			if (!bParametersValid)
				continue;

			function.bLookaheadSafe = HasLookaheadSafeAnnotation(originalLines, line);

			int32 functionOpen = 0, functionClose = 0;
			if (FindBlockBody(body, bodyOpenInBody, functionOpen, functionClose))
				function.bStub = IsStubBody(body.Mid(functionOpen + 1, functionClose - functionOpen - 1));

			module.Functions.Add(MoveTemp(function));
		}
	}

	// Overloads: the engine binds by name.
	{
		TSet<FString> seen;
		TSet<FString> reported;
		for (const FInkpotNativeBindingSourceFunction& function : module.Functions)
		{
			if (seen.Contains(function.Name) && !reported.Contains(function.Name))
			{
				AddDiagnostic(module, TEXT("error"), TEXT("cpp-overload"), FString::Printf(TEXT("'%s' is overloaded; the engine binds by name, so each function must be declared once."), *function.Name), function.Line);
				reported.Add(function.Name);
			}
			seen.Add(function.Name);
		}
	}

	if (module.Functions.Num() == 0 && module.Diagnostics.Num() == 0)
		AddDiagnostic(module, TEXT("warning"), TEXT("cpp-no-functions"), FString::Printf(TEXT("Namespace '%s%s' declares no functions, so it binds nothing."), NamespacePrefix(), *module.ModuleName), LineAtOffset(code, namespaceOffset));

	return module;
}
