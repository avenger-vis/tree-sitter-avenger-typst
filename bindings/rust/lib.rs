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
