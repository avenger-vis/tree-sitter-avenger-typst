//! The Tree-sitter grammar of the subset of Typst markup that avenger's labels support. Its
//! named nodes follow `typst-syntax`'s kinds, in snake case, and the constructs that labels don't
//! support are errors.

use tree_sitter_language::LanguageFn;

unsafe extern "C" {
    fn tree_sitter_avenger_typst() -> *const ();
}

/// The Tree-sitter language function for this grammar.
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_avenger_typst) };

/// The generated public node contract.
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The highlighting query, whose capture names are Zed's.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

#[cfg(test)]
mod tests {
    #[test]
    fn the_language_loads_and_the_highlights_compile() {
        let language = super::LANGUAGE.into();
        let mut parser = tree_sitter::Parser::new();
        parser.set_language(&language).expect("the language loads");
        tree_sitter::Query::new(&language, super::HIGHLIGHTS_QUERY).expect("highlights.scm compiles");
    }
}
