/**
 * @file The Tree-sitter grammar of the subset of Typst markup that avenger's labels support.
 * @author Jon Mease
 * @license MIT
 *
 * A label is one paragraph of Typst 0.15 markup (`avenger-typst-label/docs/typst-support.md`):
 * text, strong text and emphasis, inline raw text, escapes and line breaks, inline math, and
 * `#` expressions of code. The constructs that labels don't support are errors. Node names
 * follow `typst-syntax`'s kinds in snake case.
 *
 * Typst lexes in three modes, markup, math, and code, and the parse state decides the mode: the
 * tokens of one mode are the ones valid in its states. Whitespace and comments are extras in
 * every mode, and the places where Typst reads adjacency, such as a call's `(` or a field's `.`
 * after an expression, take immediate tokens.
 */

// The precedences of code's binary operators, from the loosest, as in typst-syntax's `BinOp`.
const PREC = {
  or: 1,
  and: 2,
  not: 3,
  compare: 4,
  additive: 5,
  multiplicative: 6,
  unary: 7,
  postfix: 8,
};

// The precedences of math: an attachment binds tighter than a fraction.
const MATH = {
  frac: 1,
  attach: 2,
};

// Typst's keywords that labels don't support. They are the global reserved set, so they are
// never identifiers, and a `#` expression that starts with one is an error.
const UNSUPPORTED_KEYWORDS = [
  "let",
  "set",
  "show",
  "context",
  "for",
  "while",
  "break",
  "continue",
  "return",
  "import",
  "include",
  "as",
];

// The opening and closing delimiters of a group in math.
const MATH_OPEN = ["(", "[", "{", "⟨", "⌈", "⌊", "[|"];
const MATH_CLOSE = [")", "]", "}", "⟩", "⌉", "⌋", "|]"];

// The math shorthands of typst-syntax's lexer, longest first.
const MATH_SHORTHANDS = [
  "<==>", "<-->", "|->>", "->>", "<<-", ">->", "<--", "-->", "<->", "<-<", "<=>", "<==", "==>",
  "<~~", "~~>", "::=", "|->", "|=>", "...", "->", "<-", "<=", ">=", "<<", ">>", "=>", "=:", ":=",
  "!=", "||", "<~", "~>", "*", "-", "~",
];

export default grammar({
  name: "avenger_typst",

  extras: $ => [/\s/, $.line_comment, $.block_comment],

  // The text of markup and inline raw text, the adjacent `(`, `[`, and `.` that continue a code
  // expression, which the scanner reads before text could take them, and the sentinel of error
  // recovery.
  //
  // `_else` is the `else` of a conditional, which text would otherwise take. `_start` is offered
  // only at the start of a label, where the scanner looks for the markers of headings and lists.
  // `_unsupported` covers markup that Typst parses and labels reject: a paragraph break, an
  // automatic link, a label, a reference, a heading or a list item, and a block equation. No rule
  // takes it, so it is always an error.
  externals: $ => [
    $._text,
    $.raw,
    $._call_paren,
    $._call_bracket,
    $._field_dot,
    $._else,
    $._hash,
    $.block_comment,
    $._dollar_open,
    $._start,
    $._unsupported_markup,
    $._error_sentinel,
  ],

  word: $ => $.ident,

  reserved: {
    global: _ => UNSUPPORTED_KEYWORDS,
  },

  conflicts: _ => [],

  rules: {
    // A label: one paragraph of markup.
    markup: $ => seq(optional($._start), repeat($._markup_item)),

    _markup_item: $ =>
      choice(
        $.text,
        $.strong,
        $.emph,
        $.raw,
        $.escape,
        $.linebreak,
        $.equation,
        $._embedded,
        $.bracketed,
      ),

    // Words and the characters between them, which the external scanner reads. A `*` or a `_`
    // between two letters or digits is text, as in `snake_case`.
    text: $ => $._text,

    // `*strong*` and `_emphasis_`. A `*` in strong text closes it, and a `_` in emphasis, as in
    // typst-syntax, which stops each at its own delimiter, so `*a _b *c* d_ e*` nests.
    strong: $ => seq("*", repeat($._strong_item), "*"),
    emph: $ => seq("_", repeat($._emph_item), "_"),
    _strong_item: $ =>
      choice($.text, $.emph, $.raw, $.escape, $.linebreak, $.equation, $._embedded, $.bracketed),
    _emph_item: $ =>
      choice($.text, $.strong, $.raw, $.escape, $.linebreak, $.equation, $._embedded, $.bracketed),

    // `[...]` in markup, which is text with its brackets, as in `Revenue [USD]`.
    bracketed: $ => seq("[", repeat($._markup_item), "]"),

    // `\#`, `\u{1F600}`, and the other escapes.
    escape: _ => token(seq("\\", choice(/u\{[0-9A-Za-z]*\}/, /[^\s]/))),

    // `\` before a space or the end: a line break.
    linebreak: _ => "\\",

    // `$...$`: an inline equation. The scanner reads the opening `$`, and rejects a block
    // equation, whose math has whitespace at both ends, as in `$ x^2 $`.
    equation: $ => seq(alias($._dollar_open, "$"), optional($.math), "$"),

    // `#` and one atomic expression of code, which a following `(`, `[`, or `.` continues only
    // when it is adjacent.
    _embedded: $ => seq(alias($._hash, "#"), $._atomic),

    line_comment: _ => token(seq("//", /[^\r\n]*/)),

    // --- Math ---

    math: $ => repeat1($._math_item),

    // A `;` outside a call is math text; in a call's arguments it would start the rows of a
    // matrix, which labels don't support.
    _math_item: $ => choice($._math_operand, $.math_frac, ",", "!", ";"),

    _math_operand: $ => choice($._math_atom, $.math_attach, $.math_delimited, $.math_call),

    _math_atom: $ =>
      choice(
        $.math_ident,
        $.math_field_access,
        $.math_text,
        $.math_shorthand,
        $.math_primes,
        $.string,
        $.escape,
        $.dot,
        $._math_embedded,
      ),

    // A name of two letters or more: a variable or a symbol, as `pi` or `arrow`.
    math_ident: _ => /[\p{L}][\p{L}\p{N}]+/,

    // `arrow.r.long`: the fields of a symbol.
    math_field_access: $ =>
      seq(
        $.math_ident,
        repeat1(seq(alias(token.immediate("."), $.dot), alias(token.immediate(/[\p{L}][\p{L}\p{N}]*/), $.math_ident))),
      ),

    // A number, or one character: a letter, a digit, or a symbol.
    math_text: _ => token(choice(/[0-9]+(\.[0-9]+)?/, /[^\s\\"#_$/^&'()\[\]{}⟨⟩⌈⌉⌊⌋,;.!\-~*0-9]/)),

    math_shorthand: _ => token(choice(...MATH_SHORTHANDS)),

    math_primes: _ => /'+/,

    dot: _ => ".",

    // `x_1`, `x^2`, `x_1^2`, `x'`
    math_attach: $ =>
      prec.left(
        MATH.attach,
        seq(
          choice($._math_atom, $.math_delimited, $.math_call),
          repeat1(choice(seq("_", $._math_script), seq("^", $._math_script))),
        ),
      ),
    _math_script: $ => choice($._math_atom, $.math_delimited, $.math_call),

    // `a/b`, `(a+b)/5`
    math_frac: $ => prec.left(MATH.frac, seq($._math_operand, "/", $._math_operand)),

    // A group between delimiters, as `(a + b)` or `[0, 1)`.
    math_delimited: $ => seq(choice(...MATH_OPEN), repeat($._math_item), choice(...MATH_CLOSE)),

    // `floor(x)`, `frac(a, b)`, `cancel(x, angle: 30deg)`
    math_call: $ =>
      seq(
        choice($.math_ident, $.math_field_access),
        token.immediate("("),
        optional(seq($._math_arg, repeat(seq(",", $._math_arg)), optional(","))),
        ")",
      ),
    _math_arg: $ => choice($.math_args_item, alias($.math_named, $.named)),
    // `angle: 30deg`: a named argument, whose value is math.
    math_named: $ => seq(field("name", alias($.math_ident, $.ident)), ":", field("value", $.math_args_item)),
    math_args_item: $ => repeat1($._math_item_no_comma),
    _math_item_no_comma: $ => choice($._math_operand, $.math_frac, "!"),

    // `#` and an atomic expression in math.
    _math_embedded: $ => seq(alias($._hash, "#"), $._atomic),

    // --- Code ---

    // An expression that a `#` starts: no binary or unary operator outside parentheses.
    _atomic: $ =>
      choice(
        $.ident,
        $._literal,
        $.parenthesized,
        $.array,
        $.dict,
        $.code_block,
        $.content_block,
        $.conditional,
        $.func_call,
        $.field_access,
        $._unsupported,
      ),

    _expr: $ => choice($._atomic, $.unary, $.binary),

    ident: _ => /[\p{L}_][\p{L}\p{N}_-]*/,

    _literal: $ => choice($.none, $.auto, $.bool, $.int, $.float, $.numeric, $.str),

    none: _ => "none",
    auto: _ => "auto",
    bool: _ => choice("true", "false"),
    int: _ => token(choice(/[0-9]+/, /0x[0-9A-Fa-f]+/, /0o[0-7]+/, /0b[01]+/)),
    float: _ => token(choice(/[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/, /[0-9]+[eE][+-]?[0-9]+/, /\.[0-9]+/)),
    numeric: _ => token(seq(/[0-9]+(\.[0-9]+)?/, choice("pt", "mm", "cm", "in", "deg", "rad", "em", "fr", "%"))),
    str: $ => $.string,
    string: _ => token(seq('"', repeat(choice(/[^"\\]/, /\\./)), '"')),

    // `(expr)`
    parenthesized: $ => seq("(", $._expr, ")"),

    // `()`, `(a,)`, `(a, b)`, `(..xs)`
    array: $ =>
      choice(
        seq("(", ")"),
        seq("(", $._expr, ",", optional(seq($._array_item, repeat(seq(",", $._array_item)), optional(","))), ")"),
        seq("(", $.spread, repeat(seq(",", $._array_item)), optional(","), ")"),
      ),
    _array_item: $ => choice($._expr, $.spread),

    // `(:)`, `(a: 1, b: 2)`. The first item is named or keyed, so a spread before it reads as an
    // array's.
    dict: $ =>
      choice(
        seq("(", ":", ")"),
        seq("(", choice($.named, $.keyed), repeat(seq(",", $._dict_item)), optional(","), ")"),
      ),
    _dict_item: $ => choice($.named, $.keyed, $.spread),
    named: $ => seq(field("name", $.ident), ":", field("value", $._expr)),
    keyed: $ => seq(field("key", $.str), ":", field("value", $._expr)),
    spread: $ => seq("..", $._expr),

    // `{ a; b }`: expressions only, whose results join.
    code_block: $ => seq("{", repeat(seq($._expr, optional(";"))), "}"),

    // `[markup]`
    content_block: $ => seq("[", repeat($._markup_item), "]"),

    // `if cond [a] else if cond2 [b] else [c]`
    conditional: $ =>
      prec.right(
        seq(
          "if",
          field("condition", $._expr),
          field("then", $._body),
          optional(seq(alias($._else, "else"), field("else", choice($.conditional, $._body)))),
        ),
      ),
    _body: $ => choice($.code_block, $.content_block),

    // `f(x)`, `f[content]`, `f(x)[content]`: the `(` and `[` are adjacent to the callee.
    func_call: $ =>
      prec.right(PREC.postfix, seq(field("callee", $._atomic), choice(seq($.args, repeat($._trailing)), repeat1($._trailing)))),
    args: $ =>
      seq(alias($._call_paren, "("), optional(seq($._arg, repeat(seq(",", $._arg)), optional(","))), ")"),
    _arg: $ => choice($._expr, $.named, $.spread),
    _trailing: $ => alias($._trailing_block, $.content_block),
    _trailing_block: $ => seq(alias($._call_bracket, "["), repeat($._markup_item), "]"),

    // `a.b`: the `.` is adjacent to the target.
    field_access: $ =>
      prec(PREC.postfix, seq(field("target", $._atomic), alias($._field_dot, "."), field("field", alias(token.immediate(/[\p{L}_][\p{L}\p{N}_-]*/), $.ident)))),

    unary: $ =>
      choice(
        prec(PREC.unary, seq(choice("-", "+"), $._expr)),
        prec(PREC.not, seq("not", $._expr)),
      ),

    binary: $ => {
      const level = (p, op) => prec.left(p, seq(field("lhs", $._expr), field("op", op), field("rhs", $._expr)));
      return choice(
        level(PREC.or, "or"),
        level(PREC.and, "and"),
        level(PREC.compare, choice("==", "!=", "<", "<=", ">", ">=", "in", seq("not", "in"))),
        level(PREC.additive, choice("+", "-")),
        level(PREC.multiplicative, choice("*", "/")),
      );
    },

    // An expression that starts with a keyword that labels don't support. The keyword is a token,
    // as the reserved set needs, and the token after it is the scanner's sentinel, which it never
    // reads, so the expression is always an error.
    _unsupported: $ => seq(choice(...UNSUPPORTED_KEYWORDS), $._error_sentinel),
  },
});
