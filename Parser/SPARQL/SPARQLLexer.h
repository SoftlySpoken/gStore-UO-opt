
// Generated from SPARQL.g4 by ANTLR 4.7.2

#pragma once


#include "antlr4-runtime.h"




class  SPARQLLexer : public antlr4::Lexer {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, T__19 = 20, 
    T__20 = 21, T__21 = 22, T__22 = 23, T__23 = 24, T__24 = 25, T__25 = 26, 
    T__26 = 27, T__27 = 28, T__28 = 29, T__29 = 30, K_NOW = 31, K_YEAR = 32, 
    K_UNION = 33, K_IF = 34, K_ASK = 35, K_ASC = 36, K_CONCAT = 37, K_IN = 38, 
    K_UNDEF = 39, K_INSERT = 40, K_MONTH = 41, K_DEFAULT = 42, K_SELECT = 43, 
    K_FLOOR = 44, K_TZ = 45, K_COPY = 46, K_CEIL = 47, K_HOURS = 48, K_DATATYPE = 49, 
    K_ISNUMERIC = 50, K_STRUUID = 51, K_CONSTRUCT = 52, K_ADD = 53, K_BOUND = 54, 
    K_NAMED = 55, K_TIMEZONE = 56, K_MIN = 57, K_ISBLANK = 58, K_UUID = 59, 
    K_BIND = 60, K_CLEAR = 61, K_INTO = 62, K_AS = 63, K_ALL = 64, K_IRI = 65, 
    K_BASE = 66, K_BY = 67, K_DROP = 68, K_LOAD = 69, K_WITH = 70, K_BNODE = 71, 
    K_WHERE = 72, K_AVG = 73, K_SAMPLE = 74, K_UCASE = 75, K_SERVICE = 76, 
    K_MINUS = 77, K_SAMETERM = 78, K_STRSTARTS = 79, K_STR = 80, K_MOVE = 81, 
    K_HAVING = 82, K_COALESCE = 83, K_STRBEFORE = 84, K_ABS = 85, K_ISLITERAL = 86, 
    K_STRAFTER = 87, K_STRLEN = 88, K_LANG = 89, K_CREATE = 90, K_DESC = 91, 
    K_MAX = 92, K_FILTER = 93, K_USING = 94, K_NOT = 95, K_STRENDS = 96, 
    K_OFFSET = 97, K_CONTAINS = 98, K_PREFIX = 99, K_MINUTES = 100, K_REPLACE = 101, 
    K_REGEX = 102, K_DELETE = 103, K_SEPARATOR = 104, K_DAY = 105, K_SILENT = 106, 
    K_STRLANG = 107, K_ORDER = 108, K_ROUND = 109, K_GRAPH = 110, K_SECONDS = 111, 
    K_URI = 112, K_DISTINCT = 113, K_EXISTS = 114, K_GROUP = 115, K_SUM = 116, 
    K_REDUCED = 117, K_FROM = 118, K_LANGMATCHES = 119, K_ISURI = 120, K_TO = 121, 
    K_ISIRI = 122, K_RAND = 123, K_STRDT = 124, K_COUNT = 125, K_DESCRIBE = 126, 
    K_VALUES = 127, K_LCASE = 128, K_OPTIONAL = 129, K_LIMIT = 130, K_SUBSTR = 131, 
    KK_INSERTDATA = 132, KK_DELETEDATA = 133, KK_ENCODE_FOR_URI = 134, KK_MD5 = 135, 
    KK_SHA1 = 136, KK_SHA256 = 137, KK_SHA384 = 138, KK_SHA512 = 139, KK_GROUP_CONCAT = 140, 
    IRIREF = 141, PNAME_NS = 142, PNAME_LN = 143, BLANK_NODE_LABEL = 144, 
    VAR1 = 145, VAR2 = 146, LANGTAG = 147, INTEGER = 148, DECIMAL = 149, 
    DOUBLE = 150, INTEGER_POSITIVE = 151, DECIMAL_POSITIVE = 152, DOUBLE_POSITIVE = 153, 
    INTEGER_NEGATIVE = 154, DECIMAL_NEGATIVE = 155, DOUBLE_NEGATIVE = 156, 
    EXPONENT = 157, STRING_LITERAL1 = 158, STRING_LITERAL2 = 159, STRING_LITERAL_LONG1 = 160, 
    STRING_LITERAL_LONG2 = 161, ECHAR = 162, NIL = 163, WS = 164, ANON = 165, 
    PN_CHARS_BASE = 166, PN_CHARS_U = 167, VARNAME = 168, PN_CHARS = 169, 
    PN_PREFIX = 170, PN_LOCAL = 171, PLX = 172, PERCENT = 173, HEX = 174, 
    PN_LOCAL_ESC = 175
  };

  SPARQLLexer(antlr4::CharStream *input);
  ~SPARQLLexer();

  virtual std::string getGrammarFileName() const override;
  virtual const std::vector<std::string>& getRuleNames() const override;

  virtual const std::vector<std::string>& getChannelNames() const override;
  virtual const std::vector<std::string>& getModeNames() const override;
  virtual const std::vector<std::string>& getTokenNames() const override; // deprecated, use vocabulary instead
  virtual antlr4::dfa::Vocabulary& getVocabulary() const override;

  virtual const std::vector<uint16_t> getSerializedATN() const override;
  virtual const antlr4::atn::ATN& getATN() const override;

private:
  static std::vector<antlr4::dfa::DFA> _decisionToDFA;
  static antlr4::atn::PredictionContextCache _sharedContextCache;
  static std::vector<std::string> _ruleNames;
  static std::vector<std::string> _tokenNames;
  static std::vector<std::string> _channelNames;
  static std::vector<std::string> _modeNames;

  static std::vector<std::string> _literalNames;
  static std::vector<std::string> _symbolicNames;
  static antlr4::dfa::Vocabulary _vocabulary;
  static antlr4::atn::ATN _atn;
  static std::vector<uint16_t> _serializedATN;


  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

  struct Initializer {
    Initializer();
  };
  static Initializer _init;
};
