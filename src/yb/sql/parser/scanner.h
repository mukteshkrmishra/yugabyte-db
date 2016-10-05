//--------------------------------------------------------------------------------------------------
// Portions Copyright (c) YugaByte, Inc.
// Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// API for the scanner.
//
// The core scanner is also used by PL/pgsql, so we provide a public API
// for it.  However, the rest of the backend is only expected to use the
// higher-level API provided by parser.h.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_PARSER_SCANNER_H_
#define YB_SQL_PARSER_SCANNER_H_

#include <stdint.h>

#include <cstddef>

// Include base lexer class. FLEX might or might not "include <FlexLexer.h>" in its code and
// generated code. The macro "yyFlexLexerOnce" is used here to guard duplicate includes.
#ifndef yyFlexLexerOnce
#include <FlexLexer.h>
#endif

#include "yb/sql/util/memory_context.h"
#include "yb/sql/parser/parse_context.h"

// Include auto-generated file from YACC.
#include "yb/sql/parser/parser_gram.y.hh"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// Various declarations that are used for keyword, identifier, and text.
//--------------------------------------------------------------------------------------------------

// Unicode support.
typedef uint32_t pg_wchar;

// Maximum length for identifiers (e.g. table names, column names, function names).  Names actually
// are limited to one less byte than this, because the length must include a trailing zero byte.
//
// Changing this requires an initdb.
constexpr int NAMEDATALEN = 64;

// UTF bit.
constexpr int UTF_HIGHBIT = 0x80;
constexpr int is_utf_highbit_set(unsigned char ch) { return (ch & UTF_HIGHBIT) != 0; }

// Keywords.
class ScanKeyword {
 public:
  //------------------------------------------------------------------------------------------------
  // Keyword categories. The value in this enum is used to characterize the keywords into different
  // groups. The group that a keyword belongs to must match with their definitions in the file
  // "parser_gram.y".
  enum class KeywordCategory : int16_t {
    UNRESERVED_KEYWORD = 0,
    COL_NAME_KEYWORD,
    TYPE_FUNC_NAME_KEYWORD,
    RESERVED_KEYWORD,
    INVALID_KEYWORD,
  };

  //------------------------------------------------------------------------------------------------
  // Public functions.
  ScanKeyword(const char* name, GramProcessor::token_type value, KeywordCategory category)
      : name_(name), value_(value), category_(category) {
  }

  bool is_valid() const {
    return category_ != KeywordCategory::INVALID_KEYWORD;
  }

  GramProcessor::token_type token() const {
    return GramProcessor::token_type(value_);
  }

  const char* name() const {
    return name_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  const char *name_;           // Name in lower case.
  int16_t value_;              // Grammar's token code.
  KeywordCategory category_;           // See codes above for different keyword categories.
};

// Scan state.
// A token might require multiple scans, and each of these calls might be passed a different
// ScanState. This callstack variable was needed in PostgreSql C-code, but we might not needed in
// our C++ code.
class ScanState {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ScanState> SharedPtr;
  typedef std::shared_ptr<const ScanState> SharedPtrConst;

  typedef std::unique_ptr<ScanState> UniPtr;
  typedef std::unique_ptr<const ScanState> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  ScanState();
  virtual ~ScanState();
};

// LexProcessor class.
class LexProcessor : public yyFlexLexer {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<LexProcessor> SharedPtr;
  typedef std::shared_ptr<const LexProcessor> SharedPtrConst;

  typedef std::unique_ptr<LexProcessor> UniPtr;
  typedef std::unique_ptr<const LexProcessor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  // Constructor and destructor.
  LexProcessor();
  virtual ~LexProcessor();

  // Reset all scanning state variables such that processing a SQL statement should not be affected
  // by the erroneous state of the precedent statements.
  void ScanInit(ParseContext *parse_context);

  // Memory pool for allocating and deallocating operating memory spaces during parsing process.
  MemoryContext *ParseMem() const {
    return parse_context_->ParseMem();
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_context_->PTreeMem();
  }

  // Entry point for lexical analysis. Scanns and return one token at a time. This is a wrapper
  // around yylex(), and it might call yylex more than once to process a token.
  GramProcessor::symbol_type Scan();

  // Counts number of newline characters in the current token and set token location accordingly.
  void CountNewlineInToken(const std::string& token);

  // Reports error.
  void ScanError(const char *message);

  // Access function for current token location.
  const location &token_loc() const {
    return token_loc_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Private types.
  enum class BackslashQuoteType {
    OFF,
    ON,
    SAFE_ENCODING
  };

  //------------------------------------------------------------------------------------------------
  // Private functions.
  // Returns a valid keyword value if it exists. Otherwise, returns an invalid value.
  static const ScanKeyword& ScanKeywordLookup(const char *text);

  // The following line lets the compilers know that we know of the existing yylex() from FLEX base
  // class, but we intend to define and use our own yylex().
  using yyFlexLexer::yylex;

  // Run lexical analysis.
  GramProcessor::symbol_type yylex(const ScanState& scan_state);

  // Returns the number of bytes that was read from the input stream or string. Lexer will call
  // this function to collect tokens from the input.
  int LexerInput(char* buf, int max_size) override;

  // Scan the input statement for the next token.
  void ScanNextToken(const ScanState& scan_state, GramProcessor::symbol_type *next_token);

  // Advance current token location by the given number of bytes.
  void AdvanceCursor(int bytes) {
    cursor_ += bytes;
  }

  //------------------------------------------------------------------------------------------------
  // NOTE: All entities below this line in this modules are copies of PostgreSql's code. We made
  // some minor changes to avoid lint errors such as using '{' for if blocks, change the comment
  // style from '/**/' to '//', and post-fix data members with "_".
  //------------------------------------------------------------------------------------------------
  // Operations on literal buffers.
  void EnlargeLiteralBuf(int bytes);
  void startlit();
  void addlit(char *ytext, int yleng);
  void addlitchar(unsigned char ychar);
  char *litbufdup();
  char *litbuf_udeescape(unsigned char escape);

  // Scan integer literal.
  GramProcessor::symbol_type process_integer_literal(const char *token);

  // Unicode support.
  unsigned char unescape_single_char(unsigned char c);
  void addunicode(pg_wchar c);
  void check_string_escape_warning(unsigned char ychar);
  void check_escape_warning();

  //------------------------------------------------------------------------------------------------
  // The context in which the scanning process is running.
  ParseContext *parse_context_;

  // The rest of this class are scanning state variables, which are declared or used by PostgreSql
  // structures, functions, and operations.
  location token_loc_;  // Current token location.
  GramProcessor::symbol_type lookahead_;  // Lookahead token.
  location cursor_;  // The current scanning location.

  // Literalbuf is used to accumulate literal values when multiple rules are needed to parse a
  // single literal.  Call startlit() to reset buffer to empty, addlit() to add text.
  // NOTE: The string in literalbuf is NOT necessarily null-terminated, but there always IS room to
  // add a trailing null at offset literallen.  We store a null only when we need it.
  char *literalbuf_;  // Temporary buffer for literal.
  int literallen_;  // Temporary buffer length.
  int literalalloc_;  // Temporary buffer size.
  int xcdepth_;  // Depth of nesting in slash-star comments.
  char *dolqstart_;  // Current $foo$ quote start string.
  int32_t utf16_first_part_;  // First of UTF16 surrogate unicode escape pair.
  bool warn_on_first_escape_;  // Literal-lexing warning for escape.
  bool saw_non_ascii_;  // Literal-lexing warning for non ascii.

  // Scanner settings to use.  These are initialized from the corresponding GUC variables by
  // scanner_init().  Callers can modify them after scanner_init() if they don't want the scanner's
  // behavior to follow the prevailing GUC settings.
  BackslashQuoteType backslash_quote_;  // State when scaning backslash.
  bool escape_string_warning_;  // State when scaning escape.
  bool standard_conforming_strings_;  // State when scaning standard string.
};

}  // namespace sql.
}  // namespace yb.

#endif  // YB_SQL_PARSER_SCANNER_H_