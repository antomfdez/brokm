# Editor support for brokm

Syntax highlighting for brokm (`.bk`) files across three editors. brokm is
HolyC-flavored, so the highlighting covers its types (`U0`..`I64`, `F64`,
`Bool`), control flow, `#include`, the `class`/`struct` object model, character
and (possibly multi-line) string literals, and the native standard library
(`Print`, `Append`, `MapNew`, `MAlloc`, ...).

| Editor        | Mechanism                    | Location                  |
|---------------|------------------------------|---------------------------|
| Neovim / Vim  | Vim regex syntax (drop-in)   | `nvim/`                   |
| Sublime Text  | `.sublime-syntax` (drop-in)  | `sublime/`                |
| Zed           | Tree-sitter extension        | `zed/`                    |
| (shared)      | Tree-sitter grammar          | `tree-sitter-brokm/`      |

The Tree-sitter grammar under `tree-sitter-brokm/` powers Zed and also serves as
the `nvim-treesitter` option. It parses every `.bk` file in this repo with zero
error nodes.

---

## Neovim / Vim (regex syntax — no build)

Drop-in, works immediately:

```sh
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect      # Neovim
cp nvim/syntax/brokm.vim   ~/.config/nvim/syntax/
cp nvim/ftdetect/brokm.vim ~/.config/nvim/ftdetect/
# For classic Vim, use ~/.vim/syntax and ~/.vim/ftdetect instead.
```

Open any `.bk` file. Filetype detection is handled by `ftdetect/brokm.vim`.

### Neovim with Tree-sitter (optional, richer)

If you use `nvim-treesitter`, register the grammar and install the queries:

```lua
local parsers = require("nvim-treesitter.parsers").get_parser_configs()
parsers.brokm = {
  install_info = {
    url = "https://github.com/antomfdez/brokm",
    location = "editors/tree-sitter-brokm",
    files = { "src/parser.c" },
  },
  filetype = "bk",
}
vim.filetype.add({ extension = { bk = "brokm" } })
```

Then `:TSInstall brokm`, and copy `tree-sitter-brokm/queries/highlights.scm` to
`~/.config/nvim/queries/brokm/highlights.scm`.

---

## Sublime Text (no build)

```sh
# Preferences > Browse Packages... opens this folder:
cp sublime/brokm.sublime-syntax \
   "~/Library/Application Support/Sublime Text/Packages/User/"   # macOS
```

`.bk` files are recognized automatically; otherwise pick it via
**View → Syntax → brokm**.

---

## Zed (Tree-sitter extension)

Zed only supports Tree-sitter, so highlighting comes from the grammar plus the
extension under `zed/`.

1. The grammar's generated `src/parser.c` is committed under
   `tree-sitter-brokm/`. After any grammar change, regenerate and commit it:

   ```sh
   cd tree-sitter-brokm && npx tree-sitter-cli generate
   ```

2. Set the grammar `commit` in `zed/extension.toml` to a pushed commit of this
   repo that contains `editors/tree-sitter-brokm/src/parser.c` (the `path` key
   already points Zed at the subdirectory).

3. Install as a dev extension: in Zed, **Extensions → Install Dev Extension**,
   and select the `zed/` directory.

Open a `.bk` file to see highlighting.

---

## Developing the grammar

```sh
cd tree-sitter-brokm
npx tree-sitter-cli generate                 # build src/parser.c from grammar.js
npx tree-sitter-cli parse ../../examples/arrays.bk   # inspect the parse tree
# Sanity-check: every .bk file should parse with no ERROR nodes.
for f in ../../examples/*.bk ../../tests/cases/*.bk; do
  npx tree-sitter-cli parse "$f" 2>&1 | grep -q ERROR && echo "FAIL $f"
done
```

`tree-sitter-brokm/queries/highlights.scm` uses `nvim-treesitter`'s granular
capture names (`@keyword.repeat`, `@function.builtin`, ...). Zed uses its own
copy at `zed/languages/brokm/highlights.scm` with the flatter capture set Zed
themes. Keep the two in sync when adding rules.
