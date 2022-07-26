/**
	// Fragments extractor.
*/
#include <clang-c/Index.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdbool.h>
#include <fault/libc.h>
#include <fault/fs.h>

int print_attribute(FILE *, char *, char *);
int print_attribute_after(FILE *, char *);
int print_attribute_start(FILE *, char *);
int print_attributes_open(FILE *);
int print_attributes_close(FILE *);

int print_number_attribute(FILE *, char *, unsigned long);
int print_number(FILE *, char *, unsigned long);
int print_expression_open(FILE *, unsigned long, unsigned long);
int print_expression_node(FILE *, const char *, unsigned long, unsigned long);
int print_expression_close(FILE *);
int print_string_attribute(FILE *, char *, CXString);
int print_string(FILE *, char *, int pcount);
int print_string_before(FILE *, char *);
int print_identifier(FILE *, char *);
int print_open(FILE *, char *);
int print_open_empty(FILE *, char *);
int print_enter(FILE *);
int print_exit(FILE *);
int print_exit_final(FILE *);
int print_close_empty(FILE *, char *);
int print_close(FILE *, char *);
int print_close_final(FILE *, char *);
int print_close_no_attributes(FILE *, char *);
int print_text(FILE *, char *, bool skip_last);
int print_area(FILE *, unsigned long, unsigned long, unsigned long, unsigned long);

struct Position {
	unsigned long ln, cn;
	/* Expansion endpoint */
	CXSourceRange xrange;
} curs;

struct Image {
	CXTranslationUnit *tu;

	FILE *elements;
	FILE *doce; /* documentation entries */
	FILE *docs;
	FILE *data;
	FILE *expr;

	/**
		// The start line and column number of the previously
		// delimited expression.
		// Closes expression series when changed.
	*/
	struct Position curs;
	CXString file;
	unsigned int line, column;

	/**
		// Track whether an #include is being visited.
		// Increment is reset whenever the main file is being processed.
	*/
	int include_depth;
};

void
image_initialize(struct Image *ctx, CXCursor root, CXTranslationUnit *tu)
{
	CXSourceRange sr = clang_getCursorExtent(root);
	CXSourceLocation start = clang_getRangeStart(sr);
	CXSourceLocation stop = clang_getRangeEnd(sr);
	unsigned int start_line, stop_line, start_column, stop_column;

	ctx->tu = tu;
	ctx->curs.ln = 0;
	ctx->curs.cn = 0;
	ctx->curs.xrange = clang_getNullRange();
	ctx->include_depth = 0;

	clang_getPresumedLocation(start, &ctx->file, &ctx->line, &ctx->column);
}

static enum CXChildVisitResult visitor(CXCursor cursor, CXCursor parent, CXClientData cd);

static char *
access_string(enum CX_CXXAccessSpecifier aspec)
{
	switch (aspec)
	{
		case CX_CXXPublic:
			return("public");
		break;

		case CX_CXXPrivate:
			return("private");
		break;

		case CX_CXXProtected:
			return("protected");
		break;

		default:
			return(NULL);
		break;
	}

	return(NULL);
}

static void
print_access(FILE *fp, CXCursor cursor)
{
	enum CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
	print_attribute(fp, "access", access_string(access));
}

static char *
storage_string(enum CX_StorageClass storage)
{
	switch (storage)
	{
		default:
		case CX_SC_Invalid:
		case CX_SC_None:
		case CX_SC_Auto:
			return(NULL);
		break;

		/*
			// Intent of this union is to handle visibility with another attribute;
			// By default, extern will be presumed to be visible unless stated otherwise.
		*/
		case CX_SC_PrivateExtern:
		case CX_SC_Extern:
			return("extern");
		break;

		case CX_SC_Static:
			return("static");
		break;

		case CX_SC_OpenCLWorkGroupLocal:
			return("local");
		break;

		case CX_SC_Register:
			return("register");
		break;
	}

	return(NULL);
}

static const char *
node_element_name(enum CXCursorKind kind)
{
	switch (kind)
	{
		case CXCursor_ObjCDynamicDecl:
			return("dynamic");
		case CXCursor_ObjCSynthesizeDecl:
			return("synthesize");

		case CXCursor_ObjCImplementationDecl:
			return("implementation");

		case CXCursor_ObjCCategoryImplDecl:
			return("category-implementation");

		case CXCursor_ObjCCategoryDecl:
			return("category");

		case CXCursor_ObjCInterfaceDecl:
			return("interface");

		case CXCursor_ObjCProtocolDecl:
			return("protocol");

		case CXCursor_TypedefDecl:
			return("typedef");

		case CXCursor_EnumDecl:
			return("enumeration");

		case CXCursor_EnumConstantDecl:
			return("constant");

		case CXCursor_MacroDefinition:
			return("macro");
		case CXCursor_MacroExpansion:
			return("expansion");

		case CXCursor_ObjCInstanceMethodDecl:
		case CXCursor_ObjCClassMethodDecl:
		case CXCursor_CXXMethod:
			return("method");

		case CXCursor_FunctionDecl:
			return("function");

		case CXCursor_UnionDecl:
			return("union");

		case CXCursor_StructDecl:
			return("structure");

		case CXCursor_ClassDecl:
			return("class");

		case CXCursor_CXXFinalAttr:
		case CXCursor_CXXOverrideAttr:
		case CXCursor_FieldDecl:
			return("field");

		case CXCursor_NamespaceAlias:
			return("namespace-alias");

		case CXCursor_Namespace:
			return("namespace");

		/*
			// Expressions and Statements
		*/
		case CXCursor_ParenExpr:
			return("enclosure");

		case CXCursor_CallExpr:
			return("invocation");
		case CXCursor_ObjCMessageExpr:
			return("message[objective-c]");

		case CXCursor_InitListExpr:
			return("initialization/list");

		case CXCursor_CStyleCastExpr:
			return("cast");
		case CXCursor_LambdaExpr:
			return("lambda");
		case CXCursor_UnaryExpr:
			return("unary");

		case CXCursor_DeclRefExpr:
			return("reference/name");
		case CXCursor_MemberRefExpr:
			return("reference/member");
		case CXCursor_ObjCSelfExpr:
			return("reference/self[objective-c]");
		case CXCursor_CXXThisExpr:
			return("reference/this[c++]");

		case CXCursor_LabelStmt:
			return("statement/label");
		case CXCursor_CaseStmt:
			return("statement/case");
		case CXCursor_DeclStmt:
			return("statement/declaration");
		case CXCursor_NullStmt:
			return("statement/null");
		case CXCursor_IfStmt:
			return("statement/if");
		case CXCursor_SwitchStmt:
			return("statement/switch");
		case CXCursor_WhileStmt:
			return("statement/while");
		case CXCursor_DoStmt:
			return("statement/do");
		case CXCursor_ForStmt:
			return("statement/for");
		case CXCursor_GotoStmt:
			return("statement/goto");
		case CXCursor_ContinueStmt:
			return("statement/continue");
		case CXCursor_BreakStmt:
			return("statement/break");
		case CXCursor_ReturnStmt:
			return("statement/return");
		case CXCursor_CompoundStmt:
			return("statement/group");

		case CXCursor_ConditionalOperator:
			return("operator/conditional");
		case CXCursor_CompoundAssignOperator:
			return("operator/compound-assignment");
		case CXCursor_UnaryOperator:
			return("operator/unary");
		case CXCursor_BinaryOperator:
			return("operator/binary");

		case CXCursor_ArraySubscriptExpr:
			return("routing/array-subscript");

		case CXCursor_IntegerLiteral:
			return("literal/integer");
		case CXCursor_FloatingLiteral:
			return("literal/float");
		case CXCursor_ImaginaryLiteral:
			return("literal/imaginary");
		case CXCursor_StringLiteral:
			return("literal/string");
		case CXCursor_CharacterLiteral:
			return("literal/character");

		case CXCursor_ObjCStringLiteral:
			return("literal/string[objective-c]");
		case CXCursor_ObjCBoolLiteralExpr:
			return("literal/bool[objective-c]");

		case CXCursor_SizeOfPackExpr:
			return("sizeof-packed[c++]");
		case CXCursor_PackExpansionExpr:
			return("pack-expansion[c++]");
		case CXCursor_CXXThrowExpr:
			return("throw[c++]");

		default:
			return("unknown");
	}

	return("switch-passed-with-default");
}

static void
print_storage(FILE *fp, CXCursor cursor)
{
	enum CX_StorageClass storage = clang_Cursor_getStorageClass(cursor);
	print_attribute(fp, "storage", storage_string(storage));
}

/*
	print_attributes_open(ctx->elements);
	{
		print_attribute(ctx->elements, "element", (char *) node_element_name(parent.kind));
		print_origin(ctx->elements, parent);
	}
	print_attributes_close(ctx->elements);

	print_close(ctx->elements, "context");
*/

static void
print_path(FILE *fp, CXCursor cursor)
{
	CXCursor parent = clang_getCursorSemanticParent(cursor);

	switch (parent.kind)
	{
		case CXCursor_TranslationUnit:
		case CXCursor_FirstInvalid:
			;
		break;

		default:
			print_path(fp, parent);
		break;
	}

	{
		CXString s = clang_getCursorSpelling(cursor);
		const char *cs = clang_getCString(s);

		if (cs != NULL)
		{
			print_string_before(fp, cs);
			clang_disposeString(s);
		}
	}
}

static void
print_origin(FILE *fp, CXCursor cursor)
{
	CXSourceRange range = clang_getCursorExtent(cursor);
	CXSourceLocation srcloc = clang_getRangeStart(range);
	CXFile file;
	unsigned int line, offset, column;

	clang_getSpellingLocation(srcloc, &file, &line, &column, &offset);
	print_string_attribute(fp, "origin", clang_getFileName(file));
}

static void
trace_location(FILE *fp, enum CXCursorKind kind, CXSourceRange range)
{
	CXSourceLocation start = clang_getRangeStart(range);
	CXSourceLocation stop = clang_getRangeEnd(range);
	CXString start_file, stop_file;
	unsigned int start_line, stop_line, start_column, stop_column;

	clang_getPresumedLocation(start, &start_file, &start_line, &start_column);
	clang_getPresumedLocation(stop, &stop_file, &stop_line, &stop_column);

	fprintf(fp, "[%d] %s: %u.%u-%u.%u\n", (int) kind,
		clang_getCString(start_file),
		start_line, start_column,
		stop_line, stop_column);
}

/**
	// Generic means to note the location of the cursor.
*/
static int
print_source_location(FILE *fp, CXSourceRange range)
{
	CXSourceLocation start = clang_getRangeStart(range);
	CXSourceLocation stop = clang_getRangeEnd(range);
	CXString start_file, stop_file;
	unsigned int start_line, stop_line, start_column, stop_column;

	clang_getPresumedLocation(start, &start_file, &start_line, &start_column);
	clang_getPresumedLocation(stop, &stop_file, &stop_line, &stop_column);

	print_area(fp, start_line, start_column, stop_line, stop_column);
	return(0);
}

/**
	// Print the expression node and move the Position &cursor
	// accordingly.
*/
static int
expression(FILE *fp, const char *ntype, CXSourceRange range, struct Position *cursor)
{
	CXSourceLocation start = clang_getRangeStart(range);
	CXSourceLocation stop = clang_getRangeEnd(range);
	CXString start_file, stop_file;
	unsigned int start_line, stop_line, start_offset, stop_offset, start_column, stop_column;

	clang_getPresumedLocation(start, &start_file, &start_line, &start_column);
	clang_getPresumedLocation(stop, &stop_file, &stop_line, &stop_column);

	/*
		// Change expression context.
	*/
	if (start_line != cursor->ln || start_column != cursor->cn)
	{
		if (cursor->ln != 0)
		{
			/* Not the initial expression */
			print_expression_close(fp);
		}

		print_expression_open(fp, start_line, start_column);
		cursor->ln = start_line;
		cursor->cn = start_column;
	}

	print_expression_node(fp, ntype, stop_line, stop_column > 0 ? stop_column - 1 : 0);
	return(0);
}

static int
print_spelling_identifier(FILE *fp, CXCursor c)
{
	CXString s = clang_getCursorSpelling(c);
	const char *cs = clang_getCString(s);

	print_identifier(fp, (char *) cs);
	clang_disposeString(s);

	return(0);
}

static int
print_type_class(FILE *fp, enum CXTypeKind k)
{
	switch (k)
	{
		case CXType_Enum:
			print_attribute(fp, "meta", "enum");
		break;

		case CXType_IncompleteArray:
			print_attribute(fp, "meta", "array");
		break;

		case CXType_VariableArray:
			print_attribute(fp, "meta", "array");
		break;

		case CXType_Vector:
			print_attribute(fp, "meta", "vector");
		break;

		case CXType_Typedef:
			print_attribute(fp, "meta", "typedef");
		break;

		default:
			if (k >= 100)
				print_attribute(fp, "meta", "pointer");
			else
				print_attribute(fp, "meta", "data");
		break;
	}

	return(0);
}

static int
print_qualifiers(FILE *fp, CXType t)
{
	int c = 0;

	print_enter(fp);
	{
		if (clang_isConstQualifiedType(t))
		{
			print_string(fp, "const", c);
			c += 1;
		}

		if (clang_isVolatileQualifiedType(t))
		{
			print_string(fp, "volatile", c);
			c += 1;
		}

		if (clang_isRestrictQualifiedType(t))
		{
			print_string(fp, "restrict", c);
			c += 1;
		}
	}
	print_exit(fp);
	return(0);
}

static int
print_type(FILE *fp, CXCursor c, CXType ct)
{
	CXCursor dec = clang_getTypeDeclaration(ct);
	CXType xt = ct;
	enum CXTypeKind k = xt.kind;
	unsigned long indirection_level = 0;
	long long i = -1;

	while (k >= 100)
	{
		if (k == CXType_Typedef)
			break;

		xt = clang_getPointeeType(xt);
		k = xt.kind;
		++indirection_level;
	}

	print_enter(fp);
	if (0)
	{
		xt = ct;
		k = ct.kind;

		if (!print_qualifiers(fp, xt))
		{
			while (k >= 100)
			{
				xt = clang_getPointeeType(xt);
				k = xt.kind;
				if (print_qualifiers(fp, xt))
					break;
			}
		}
	}
	print_exit(fp);

	print_attributes_open(fp);
	if (!clang_Cursor_isNull(dec) && dec.kind != CXCursor_NoDeclFound)
	{
		print_spelling_identifier(fp, dec);
		print_origin(fp, dec);
	}
	else
	{
		print_string_attribute(fp, "identifier", clang_getTypeSpelling(xt));
	}

	print_string_attribute(fp, "syntax", clang_getTypeSpelling(ct));
	print_string_attribute(fp, "kind", clang_getTypeKindSpelling(k));

	i = clang_Type_getAlignOf(xt);
	if (i >= 0)
		print_number_attribute(fp, "align", i);

	i = clang_Type_getSizeOf(xt);
	if (i >= 0)
		print_number_attribute(fp, "size", i);

	i = clang_getArraySize(xt);
	if (i >= 0)
	{
		CXType at = xt;
		print_string(fp, "elements", 0);
		print_enter(fp);

		do {
			print_number(fp, NULL, i);
			at = clang_getArrayElementType(at);
			i = clang_getArraySize(at);
		} while (at.kind != CXType_Invalid && i >= 0);

		print_exit(fp);
	}

	print_attributes_close(fp);
	return(0);
}

static bool
print_comment(struct Image *ctx, CXCursor cursor)
{
	CXString comment = clang_Cursor_getRawCommentText(cursor);
	const char *comment_str = clang_getCString(comment);

	if (comment_str != NULL)
	{
		fputs("[", ctx->doce);
		print_path(ctx->doce, cursor);
		fputs("],", ctx->doce);

		fputs("[\x22", ctx->docs);
		print_text(ctx->docs, (char *) comment_str, true);
		fputs("\x22],", ctx->docs);

		clang_disposeString(comment);
	}
	else
		return(false);

	return(true);
}

static int
print_documented(FILE *fp, CXCursor cursor)
{
	CXSourceRange docarea = clang_Cursor_getCommentRange(cursor);

	if (!clang_Range_isNull(docarea))
	{
		print_attribute_after(fp, "documented");
		print_source_location(fp, docarea);
	}

	return(0);
}

/**
	// Describe the callable's type and parameters.
*/
static enum CXChildVisitResult
callable(
	CXCursor parent, CXCursor cursor, CXClientData cd,
	enum CXCursorKind kind,
	enum CXVisibilityKind vis,
	enum CXAvailabilityKind avail)
{
	struct Image *ctx = (struct Image *) cd;
	CXCursor arg;
	int i, nargs = clang_Cursor_getNumArguments(cursor);

	print_open(ctx->elements, "type");
	print_type(ctx->elements, cursor, clang_getResultType(clang_getCursorType(cursor)));
	print_close(ctx->elements, "type");

	for (i = 0; i < nargs; ++i)
	{
		CXCursor arg = clang_Cursor_getArgument(cursor, i);
		CXType ct = clang_getCursorType(arg);

		print_open(ctx->elements, "parameter");
		print_enter(ctx->elements);
		{
			print_open(ctx->elements, "type");
			print_type(ctx->elements, arg, ct);
			print_close(ctx->elements, "type");
		}
		print_exit(ctx->elements);

		print_attributes_open(ctx->elements);
		{
			print_spelling_identifier(ctx->elements, arg);
		}
		print_attributes_close(ctx->elements);

		print_close(ctx->elements, "parameter");
	}

	return(CXChildVisit_Continue);
}

static enum CXChildVisitResult
macro(
	CXCursor parent, CXCursor cursor, CXClientData cd,
	enum CXCursorKind kind,
	enum CXVisibilityKind vis,
	enum CXAvailabilityKind avail)
{
	struct Image *ctx = (struct Image *) cd;
	CXCursor arg;
	int i = 0, nargs = clang_Cursor_getNumArguments(cursor);

	print_comment(ctx, cursor);

	print_enter(ctx->elements);
	{
		if (clang_Cursor_isMacroFunctionLike(cursor))
		{
			while (i < nargs)
			{
				CXCursor arg = clang_Cursor_getArgument(cursor, i);

				print_open_empty(ctx->elements, "parameter");
				print_attributes_open(ctx->elements);
				{
					print_spelling_identifier(ctx->elements, arg);
				}
				print_attributes_close(ctx->elements);
				print_close(ctx->elements, "parameter");

				++i;
			}
		}
	}
	print_exit(ctx->elements);

	print_attributes_open(ctx->elements);
	{
		print_spelling_identifier(ctx->elements, cursor);
		print_attribute_start(ctx->elements, "area");
		print_source_location(ctx->elements, clang_getCursorExtent(cursor));
		print_documented(ctx->elements, cursor);
	}
	print_attributes_close(ctx->elements);

	return(CXChildVisit_Continue);
}

static enum CXChildVisitResult
print_enumeration(
	CXCursor parent, CXCursor cursor, CXClientData cd,
	enum CXCursorKind kind,
	enum CXVisibilityKind vis,
	enum CXAvailabilityKind avail)
{
	struct Image *ctx = (struct Image *) cd;
	CXType t;

	print_comment(ctx, cursor);

	t = clang_getEnumDeclIntegerType(cursor);
	print_enter(ctx->elements);
	{
		clang_visitChildren(cursor, visitor, cd);
	}
	print_exit(ctx->elements);

	print_attributes_open(ctx->elements);
	{
		print_spelling_identifier(ctx->elements, cursor);
		print_attribute_start(ctx->elements, "area");
		print_source_location(ctx->elements, clang_getCursorExtent(cursor));
		print_documented(ctx->elements, cursor);
	}
	print_attributes_close(ctx->elements);

	return(CXChildVisit_Continue);
}

static void
print_collection(struct Image *ctx, CXCursor cursor, CXClientData cd, const char *element_name)
{
	print_comment(ctx, cursor);

	print_open(ctx->elements, (char *) element_name);

	print_enter(ctx->elements);
	{
		clang_visitChildren(cursor, visitor, cd);
	}
	print_exit(ctx->elements);

	print_attributes_open(ctx->elements);
	{
		print_spelling_identifier(ctx->elements, cursor);
		print_attribute_start(ctx->elements, "area");
		print_source_location(ctx->elements, clang_getCursorExtent(cursor));
		print_documented(ctx->elements, cursor);
	}
	print_attributes_close(ctx->elements);

	print_close(ctx->elements, (char *) element_name);
}

/**
	// Visit the declaration nodes emitting structure and documentation strings.
*/
static enum CXChildVisitResult
visitor(CXCursor cursor, CXCursor parent, CXClientData cd)
{
	struct Image *ctx = (struct Image *) cd;
	static CXString last_file = {0,};
	enum CXChildVisitResult ra = CXChildVisit_Recurse;

	enum CXCursorKind kind = clang_getCursorKind(cursor);
	enum CXVisibilityKind vis = clang_getCursorVisibility(cursor);
	enum CXAvailabilityKind avail = clang_getCursorAvailability(cursor);

	CXSourceLocation location = clang_getCursorLocation(cursor);

	if (clang_Location_isFromMainFile(location) == 0)
	{
		/*
			// Ignore if inside an include/import.
			// Expansion regions are located at the origin of their definition.
			// However, the presumed location is bound to the reference point,
			// so when the cursor is not inside an include and is not in the
			// main file, it is known to be inside of an expansion.
		*/
		if (ctx->include_depth == 0)
		{
			CXSourceRange range = clang_getCursorExtent(cursor);
			CXSourceLocation start = clang_getRangeStart(range);
			CXSourceLocation stop = clang_getRangeEnd(range);
			CXString start_file, stop_file;
			unsigned int start_line, stop_line, start_column, stop_column;

			clang_getPresumedLocation(start, &start_file, &start_line, &start_column);
			clang_getPresumedLocation(stop, &stop_file, &stop_line, &stop_column);

			if (clang_getCString(start_file) == clang_getCString(ctx->file))
			{
				/* Hold final range to use as expansion node. */
				ctx->curs.xrange = range;
			}
		}

		return(CXChildVisit_Recurse);
	}
	else if (ctx->include_depth > 0)
	{
		/* Back in the main file. */
		ctx->include_depth = 0;
	}
	else if (!clang_Range_isNull(ctx->curs.xrange))
	{
		expression(ctx->expr, "expansion", ctx->curs.xrange, &(ctx->curs));
		ctx->curs.xrange = clang_getNullRange();
	}

	switch (kind)
	{
		case CXCursor_TypedefDecl:
		{
			CXType real_type = clang_getTypedefDeclUnderlyingType(cursor);

			print_comment(ctx, cursor);
			print_open(ctx->elements, "typedef");

			print_enter(ctx->elements);
			{
				print_type(ctx->elements, cursor, real_type);
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
				print_attribute_start(ctx->elements, "area");
				print_source_location(ctx->elements, clang_getCursorExtent(cursor));
				print_documented(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "typedef");
		}
		break;

		case CXCursor_EnumDecl:
		{
			print_open(ctx->elements, "enumeration");
			print_enumeration(parent, cursor, cd, kind, vis, avail);
			print_close(ctx->elements, "enumeration");
		}
		break;

		case CXCursor_EnumConstantDecl:
		{
			print_open_empty(ctx->elements, "constant");

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
				print_number_attribute(ctx->elements, "integer",
					clang_getEnumConstantDeclValue(cursor));
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "constant");
		}
		break;

		case CXCursor_InclusionDirective:
		{
			CXFile ifile;
			CXString ifilename;

			ifile = clang_getIncludedFile(cursor);
			ifilename = clang_getFileName(ifile);

			print_open_empty(ctx->elements, "include");
			{
				print_attributes_open(ctx->elements);
				{
					print_attribute(ctx->elements, "system", (char *) clang_getCString(ifilename));
					print_attribute_start(ctx->elements, "area");
					print_source_location(ctx->elements, clang_getCursorExtent(cursor));
				}
				print_attributes_close(ctx->elements);
			}
			print_close(ctx->elements, "include");

			ctx->include_depth += 1;

			/* Avoid as much include content as possible. */
			ra = CXChildVisit_Continue;
		}
		break;

		case CXCursor_MacroDefinition:
		{
			const char *m_subtype = "macro";

			if (!clang_Cursor_isMacroFunctionLike(cursor))
				m_subtype = "define";

			print_open(ctx->elements, (char *) m_subtype);
			macro(parent, cursor, cd, kind, vis, avail);
			print_close(ctx->elements, (char *) m_subtype);
		}
		break;

		case CXCursor_ObjCInstanceMethodDecl:
		case CXCursor_ObjCClassMethodDecl:
		case CXCursor_CXXMethod:
		{
			CXCursor cclass;

			if (clang_isCursorDefinition(cursor) == 0)
				return(ra);

			print_comment(ctx, cursor);
			print_open(ctx->elements, "method");

			print_enter(ctx->elements);
			{
				callable(parent, cursor, cd, kind, vis, avail);
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
				print_attribute_start(ctx->elements, "area");
				print_source_location(ctx->elements, clang_getCursorExtent(cursor));
				print_documented(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			clang_visitChildren(cursor, visitor, cd);
			print_close(ctx->elements, "method");
			ra = CXChildVisit_Continue;
		}
		break;

		case CXCursor_FunctionDecl:
		{
			if (clang_isCursorDefinition(cursor) == 0)
				return(ra);

			print_comment(ctx, cursor);
			print_open(ctx->elements, "function");

			print_enter(ctx->elements);
			{
				callable(parent, cursor, cd, kind, vis, avail);
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
				print_attribute_start(ctx->elements, "area");
				print_source_location(ctx->elements, clang_getCursorExtent(cursor));
				print_documented(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "function");
		}
		break;

		case CXCursor_UnionDecl:
		{
			if (clang_isCursorDefinition(cursor))
				return(ra);

			print_comment(ctx, cursor);

			print_open(ctx->elements, "union");
			print_enter(ctx->elements);
			{
				clang_visitChildren(cursor, visitor, cd);
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "union");
			ra = CXChildVisit_Continue;
		}
		break;

		case CXCursor_StructDecl:
			print_collection(ctx, cursor, cd, "structure");
			ra = CXChildVisit_Continue;
		break;

		case CXCursor_ObjCImplementationDecl:
		case CXCursor_ObjCCategoryImplDecl:
			print_collection(ctx, cursor, cd, "implementation");
			ra = CXChildVisit_Continue;
		break;

		case CXCursor_ObjCInterfaceDecl:
		case CXCursor_ObjCCategoryDecl:
			print_collection(ctx, cursor, cd, "interface");
			ra = CXChildVisit_Continue;
		break;

		case CXCursor_ObjCProtocolDecl:
			print_collection(ctx, cursor, cd, "protocol");
			ra = CXChildVisit_Continue;
		break;

		case CXCursor_ClassDecl:
			print_collection(ctx, cursor, cd, "class");
			ra = CXChildVisit_Continue;
		break;

		case CXCursor_IBActionAttr:
		case CXCursor_IBOutletAttr:
		case CXCursor_IBOutletCollectionAttr:
		case CXCursor_CXXFinalAttr:
		case CXCursor_CXXOverrideAttr:
		case CXCursor_ObjCPropertyDecl:
		case CXCursor_ObjCIvarDecl:
		case CXCursor_FieldDecl:
		{
			print_comment(ctx, cursor);

			print_open(ctx->elements, "field");

			print_enter(ctx->elements);
			{
				print_open(ctx->elements, "type");
				print_type(ctx->elements, cursor, clang_getCursorType(cursor));
				print_close(ctx->elements, "type");
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "field");
		}
		break;

		case CXCursor_NamespaceAlias:
		{
			print_open(ctx->elements, "namespace");

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
				print_attribute(ctx->elements, "target", "...");
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "namespace");
		}
		break;

		case CXCursor_Namespace:
		{
			print_comment(ctx, cursor);
			print_open(ctx->elements, "namespace");

			print_enter(ctx->elements);
			{
				clang_visitChildren(cursor, visitor, cd);
			}
			print_exit(ctx->elements);

			print_attributes_open(ctx->elements);
			{
				print_spelling_identifier(ctx->elements, cursor);
			}
			print_attributes_close(ctx->elements);

			print_close(ctx->elements, "namespace");
		}
		break;

		case CXCursor_ObjCSynthesizeDecl:
		case CXCursor_ObjCDynamicDecl:
		case CXCursor_TypeAliasDecl:
		case CXCursor_IntegerLiteral:
		case CXCursor_FloatingLiteral:
		case CXCursor_ImaginaryLiteral:
		case CXCursor_StringLiteral:
		case CXCursor_CharacterLiteral:
		{
			/* Explicit continue cases; skip expression reporting. */
			ra = CXChildVisit_Continue;
		}
		break;

		case CXCursor_FirstExpr:
		case CXCursor_CompoundStmt:
		case CXCursor_UnexposedStmt:
		case CXCursor_LastStmt:
		case CXCursor_NullStmt:
		case CXCursor_ParenExpr:
		case CXCursor_PreprocessingDirective:
		{
			/* Explicit recurse cases; skip expression reporting. */
			ra = CXChildVisit_Recurse;
		}
		break;

		default:
		{
			if (clang_isExpression(kind) || clang_isStatement(kind))
			{
				expression(ctx->expr,
					node_element_name(kind), clang_getCursorExtent(cursor), &(ctx->curs));
			}
		}
		break;
	}

	return(ra);
}

int
main(int argc, const char *argv[])
{
	int i;
	struct Image ctx;
	CXIndex idx = clang_createIndex(0, 1);
	CXCursor rc;
	CXTranslationUnit u;
	enum CXErrorCode err;
	const char *output = NULL;

	/*
		// clang_parseTranslationUnit does not appear to agree that the
		// executable should end in (filename)`.i` so adjust the command name.
		// It would appear that the option parser is (was) sensitive to dot-suffixes.
	*/
	argv[0] = "delineate";

	/*
		// libclang doesn't provide access to parsed options. Scan for -o.
	*/
	i = 1;
	while (i < argc)
	{
		if (strcmp(argv[i], "-o") == 0)
		{
			output = argv[i+1];
			break;
		}

		++i;
	}
	if (output == NULL)
	{
		perror("no output file designated with -o");
		return(1);
	}

	err = clang_parseTranslationUnit2(idx, NULL, argv, argc, NULL, 0,
		CXTranslationUnit_DetailedPreprocessingRecord, &u);
	if (err != 0)
		return(1);

	rc = clang_getTranslationUnitCursor(u);

	/*
		// Parsed options accessors are not available in libclang?
	*/
	if (fs_mkdir(output) != 0)
	{
		perror("could not create target directory");
		return(1);
	}

	chdir(output);

	ctx.elements = fopen("elements.json", "w");
	ctx.doce = fopen("documented.json", "w");
	ctx.docs = fopen("documentation.json", "w");
	ctx.data = fopen("data.json", "w");
	ctx.expr = fopen("expressions.json", "w");

	image_initialize(&ctx, rc, &u);

	print_open(ctx.elements, "unit"); /* Translation Unit */
	print_enter(ctx.data);
	print_enter(ctx.docs);
	print_enter(ctx.doce);
	print_enter(ctx.expr);

	print_enter(ctx.elements);
	{
		clang_visitChildren(rc, visitor, (CXClientData) &ctx);
	}
	print_exit(ctx.elements);

	print_attributes_open(ctx.elements);
	{
		print_string_attribute(ctx.elements, "version", clang_getClangVersion());
		print_attribute(ctx.elements, "engine", "libclang");

		switch (clang_getCursorLanguage(rc))
		{
			case CXLanguage_C:
				print_attribute(ctx.elements, "language", "c");
			break;

			case CXLanguage_ObjC:
				print_attribute(ctx.elements, "language", "objective-c");
			break;

			case CXLanguage_CPlusPlus:
				print_attribute(ctx.elements, "language", "c++");
			break;

			case CXLanguage_Invalid:
			default:
			break;
		}

		/* Target triple attribute. */
		{
			CXString ts;
			CXTargetInfo ti;
			ti = clang_getTranslationUnitTargetInfo(u);
			ts = clang_TargetInfo_getTriple(ti);
			print_attribute(ctx.elements, "target", (char *) clang_getCString(ts));
			clang_TargetInfo_dispose(ti);
		}
	}
	print_attributes_close(ctx.elements);

	print_exit_final(ctx.expr);
	print_exit_final(ctx.expr);
	print_exit_final(ctx.doce);
	print_exit_final(ctx.docs);
	print_exit_final(ctx.data);
	print_close_final(ctx.elements, "unit");

	fclose(ctx.elements);
	fclose(ctx.docs);
	fclose(ctx.doce);
	fclose(ctx.data);
	fclose(ctx.expr);

	clang_disposeTranslationUnit(u);
	clang_disposeIndex(idx);

	return(0);
}
