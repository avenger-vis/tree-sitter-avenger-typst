//! The agreement of the grammar with typst-syntax 0.15.1, the parser that renders avenger's
//! labels, on the corpus of `test/agreement/cases.txt`.
//!
//! For each case, both parsers read the markup, and the test compares two things:
//!
//! - **Errors.** A case that labels support has no error in either tree, and a case that they
//!   reject is an error in the grammar's tree, whether typst-syntax rejects it or the label
//!   engine does after parsing.
//! - **Classes.** Each character that isn't whitespace has a highlight class in each tree, from
//!   its leaf: text, a delimiter of strong text or emphasis, an escape, raw text, math, code, and
//!   so on. Comparing characters rather than leaves ignores how each parser splits text.
//!
//! The test writes its report to `tests/output/agreement.txt`.

use std::{collections::BTreeMap, fmt::Write, fs, path::PathBuf};

use tree_sitter::{Node, Parser};
use typst_syntax::{LinkedNode, SyntaxKind as K};

/// The highlight class of a character.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
enum Class {
    Text,
    MarkupDelim,
    Escape,
    Linebreak,
    Raw,
    MathDelim,
    MathIdent,
    MathText,
    MathOp,
    Hash,
    Ident,
    Keyword,
    Number,
    Str,
    Op,
    Punct,
    Comment,
    Error,
}

/// The cases of the corpus: whether labels support each one, and its markup.
fn cases() -> Vec<(bool, String)> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("test/agreement/cases.txt");
    fs::read_to_string(path)
        .expect("the corpus")
        .lines()
        .filter(|line| !line.starts_with('#') && !line.trim().is_empty())
        .map(|line| {
            let (expect, text) = line.split_once('\t').expect("a tab after `ok` or `no`");
            (expect == "ok", text.replace("\\n", "\n"))
        })
        .collect()
}

/// Whether a leaf of typst-syntax is in math: an ancestor is an equation before any code.
fn in_math(node: &LinkedNode) -> bool {
    let mut parent = node.parent();
    while let Some(p) = parent {
        match p.kind() {
            K::Math | K::Equation => return true,
            K::Code | K::CodeBlock | K::ContentBlock | K::Args | K::Markup => return false,
            _ => parent = p.parent(),
        }
    }
    false
}

fn in_kind(node: &LinkedNode, kind: K) -> bool {
    let mut parent = node.parent();
    while let Some(p) = parent {
        if p.kind() == kind {
            return true;
        }
        parent = p.parent();
    }
    false
}

/// The class of a leaf of typst-syntax, or `None` for whitespace.
fn typst_class(node: &LinkedNode) -> Option<Class> {
    let kind = node.kind();
    if in_kind(node, K::Raw) {
        return Some(Class::Raw);
    }
    let text = node.leaf_text();
    let bracket = matches!(text.as_str(), "(" | ")" | "[" | "]" | "{" | "}" | "⟨" | "⟩" | "⌈" | "⌉" | "⌊" | "⌋" | "[|" | "|]");
    let math = in_math(node);
    Some(match kind {
        K::Space | K::Parbreak => return None,
        K::Error => Class::Error,
        K::LineComment | K::BlockComment => Class::Comment,
        K::Text | K::Shorthand | K::SmartQuote | K::Link | K::Label | K::RefMarker | K::HeadingMarker
        | K::ListMarker | K::EnumMarker | K::TermMarker => Class::Text,
        K::Colon if !math && node.parent_kind() == Some(K::Markup) => Class::Text,
        K::Star | K::Underscore if !math && matches!(node.parent_kind(), Some(K::Strong | K::Emph)) => {
            Class::MarkupDelim
        }
        K::Escape => Class::Escape,
        K::Linebreak => Class::Linebreak,
        K::Dollar => Class::MathDelim,
        K::MathIdent => Class::MathIdent,
        _ if math && bracket => Class::Punct,
        K::MathText => Class::MathText,
        K::MathShorthand | K::MathPrimes | K::Hat | K::Bang | K::Root | K::MathAlignPoint => Class::MathOp,
        K::Underscore | K::Slash if math => Class::MathOp,
        K::Hash => Class::Hash,
        K::Ident => Class::Ident,
        K::None | K::Auto | K::Bool | K::Not | K::And | K::Or | K::If | K::Else | K::In | K::Let | K::Set
        | K::Show | K::Context | K::For | K::While | K::Break | K::Continue | K::Return | K::Import
        | K::Include | K::As => Class::Keyword,
        K::Int | K::Float | K::Numeric => Class::Number,
        K::Str => Class::Str,
        K::Plus | K::Minus | K::Star | K::Slash | K::Eq | K::EqEq | K::ExclEq | K::Lt | K::LtEq | K::Gt
        | K::GtEq | K::PlusEq | K::HyphEq | K::StarEq | K::SlashEq | K::Arrow => Class::Op,
        K::LeftBrace | K::RightBrace | K::LeftBracket | K::RightBracket | K::LeftParen | K::RightParen
        | K::Comma | K::Semicolon | K::Colon | K::Dot | K::Dots => Class::Punct,
        _ => Class::Error,
    })
}

/// The class of a leaf of the grammar.
fn grammar_class(node: Node) -> Class {
    if node.is_error() || node.is_missing() || node.parent().is_some_and(|p| p.is_error()) {
        return Class::Error;
    }
    let parent = node.parent().map(|p| p.kind()).unwrap_or_default();
    let in_math = {
        let mut n = node.parent();
        let mut found = false;
        while let Some(p) = n {
            match p.kind() {
                "math" | "equation" => {
                    found = true;
                    break;
                }
                "content_block" | "args" | "code_block" | "parenthesized" | "array" | "dict" => break,
                _ => n = p.parent(),
            }
        }
        found
    };
    match node.kind() {
        "text" => Class::Text,
        "*" | "_" if matches!(parent, "strong" | "emph") => Class::MarkupDelim,
        // Brackets in markup are text, as Typst's are, and a comma or a `!` in math outside a
        // call's arguments is math text.
        "[" | "]" if parent == "bracketed" => Class::Text,
        "," | ";" if in_math && parent != "math_call" => Class::MathText,
        "!" if in_math => Class::MathText,
        "escape" => Class::Escape,
        "linebreak" => Class::Linebreak,
        "raw" => Class::Raw,
        "$" => Class::MathDelim,
        "math_ident" => Class::MathIdent,
        "math_text" => Class::MathText,
        "math_shorthand" | "math_primes" | "^" | "!" => Class::MathOp,
        "_" | "/" if in_math => Class::MathOp,
        "dot" | "," | ";" | ":" | "(" | ")" | "[" | "]" | "{" | "}" | "⟨" | "⟩" | "⌈" | "⌉" | "⌊" | "⌋" | "[|"
        | "|]" | "." | ".." => Class::Punct,
        "#" => Class::Hash,
        "ident" => Class::Ident,
        "none" | "auto" | "true" | "false" | "not" | "and" | "or" | "if" | "else" | "in" => Class::Keyword,
        "int" | "float" | "numeric" => Class::Number,
        "string" => Class::Str,
        "+" | "-" | "*" | "/" | "==" | "!=" | "<" | "<=" | ">" | ">=" => Class::Op,
        "line_comment" | "block_comment" => Class::Comment,
        _ => Class::Error,
    }
}

/// The class of each byte of `text` that typst-syntax gives, without whitespace.
fn typst_classes(text: &str) -> (BTreeMap<usize, Class>, bool) {
    let root = typst_syntax::parse(text);
    let mut classes = BTreeMap::new();
    let mut stack = vec![LinkedNode::new(&root)];
    while let Some(node) = stack.pop() {
        if node.children().len() > 0 {
            stack.extend(node.children());
            continue;
        }
        let Some(class) = typst_class(&node) else {
            continue;
        };
        for (offset, c) in node.leaf_text().char_indices() {
            if !c.is_whitespace() {
                classes.insert(node.offset() + offset, class);
            }
        }
    }
    (classes, !root.errors_and_warnings().0.is_empty())
}

/// The class of each byte of `text` that the grammar gives, without whitespace.
fn grammar_classes(parser: &mut Parser, text: &str) -> (BTreeMap<usize, Class>, bool, String) {
    let tree = parser.parse(text, None).expect("a tree");
    let root = tree.root_node();
    let mut classes = BTreeMap::new();
    let mut stack = vec![root];
    while let Some(node) = stack.pop() {
        if node.child_count() > 0 {
            let mut cursor = node.walk();
            stack.extend(node.children(&mut cursor));
            continue;
        }
        let class = grammar_class(node);
        let range = node.byte_range();
        for (offset, c) in text[range.clone()].char_indices() {
            if !c.is_whitespace() {
                classes.insert(range.start + offset, class);
            }
        }
    }
    (classes, root.has_error(), root.to_sexp())
}

#[test]
fn the_grammar_agrees_with_typst_syntax() {
    let mut parser = Parser::new();
    parser.set_language(&tree_sitter_avenger_typst::LANGUAGE.into()).unwrap();
    let mut report = String::new();
    let (mut errors_agree, mut classes_agree, mut total) = (0, 0, 0);
    let mut accepted_unsupported = Vec::new();
    let mut rejected_supported = Vec::new();
    let mut class_diffs = Vec::new();
    for (supported, text) in cases() {
        total += 1;
        let (typst, typst_error) = typst_classes(&text);
        let (grammar, grammar_error, sexp) = grammar_classes(&mut parser, &text);
        let expect_error = !supported || typst_error;
        if expect_error == grammar_error {
            errors_agree += 1;
        } else if grammar_error {
            rejected_supported.push(format!("{text:?}\n    {sexp}"));
        } else {
            accepted_unsupported.push(format!("{text:?}\n    {sexp}"));
        }
        // Compare classes only where both trees are free of errors.
        if typst_error || grammar_error {
            continue;
        }
        let diffs: Vec<String> = typst
            .iter()
            .filter_map(|(offset, class)| {
                let other = grammar.get(offset).copied();
                (other != Some(*class)).then(|| {
                    let c = text[*offset..].chars().next().unwrap();
                    format!("{c:?}@{offset}: typst {class:?}, grammar {other:?}")
                })
            })
            .collect();
        if diffs.is_empty() {
            classes_agree += 1;
        } else {
            class_diffs.push(format!("{text:?}\n    {}\n    {sexp}", diffs.join("\n    ")));
        }
    }
    writeln!(report, "cases: {total}, errors agree: {errors_agree}, classes agree: {classes_agree}\n").unwrap();
    for (title, list) in [
        ("Supported cases that the grammar rejects", &rejected_supported),
        ("Rejected cases that the grammar accepts", &accepted_unsupported),
        ("Classes that differ", &class_diffs),
    ] {
        writeln!(report, "## {title} ({})\n", list.len()).unwrap();
        for item in list {
            writeln!(report, "{item}\n").unwrap();
        }
    }
    let output = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/output");
    fs::create_dir_all(&output).unwrap();
    fs::write(output.join("agreement.txt"), &report).unwrap();
    println!("{}", report.lines().next().unwrap());
}
