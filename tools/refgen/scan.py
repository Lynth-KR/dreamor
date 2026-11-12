# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Reading a header: each declaration at namespace or class scope, with the /// block above it. No C++ is parsed. A
scanner follows the brackets, with comments, literals and preprocessor lines blanked out, and tells the scopes they
open - a namespace, a class, a function's body, an initializer - well enough to find each declaration. A declaration
is kept as written, up to its body, its initializer or its `;`.
"""

import dataclasses
import re
from pathlib import Path

from refgen.common import IDENT, Problems

PRIVATE_NAMESPACES = {"internal", "detail"}
NOT_A_NAME = {"decltype", "noexcept", "requires", "alignas", "sizeof", "alignof", "explicit", "static_assert"}
CLASS_KEYS = ("class", "struct", "union")
# Specifier macros: they tell a compiler how to inline a function or what a parameter's lifetime is tied to, and are
# no part of what a user calls, so a signature is shown without them.
SPECIFIER_MACROS = ("DREAMOR_INLINE", "DREAMOR_NOINLINE", "DREAMOR_LIFETIMEBOUND")
DROPPED = re.compile(r"\[\[nodiscard\]\]\s*|\b(" + "|".join(SPECIFIER_MACROS) + r")\b\s*")
# A class's member types with the standard's names say what the standard says of them, and carry no /// block.
STANDARD_MEMBER_TYPES = {
    "value_type",
    "key_type",
    "mapped_type",
    "size_type",
    "difference_type",
    "reference",
    "const_reference",
    "pointer",
    "const_pointer",
    "iterator",
    "const_iterator",
    "iterator_category",
    "iterator_concept",
    "element_type",
}


# ---------------------------------------------------------------------------------------------------------------
# Reading a header


def blank(text: str) -> str:
    """The text with comments, literals and preprocessor lines turned to spaces, its newlines kept."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True

    def wipe(start: int, end: int) -> None:
        for k in range(start, end):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        ch = text[i]
        if ch == "\n":
            line_start = True
            i += 1
            continue
        if line_start and ch == "#":
            end = i
            while end < n and not (text[end] == "\n" and text[end - 1] != "\\"):
                end += 1
            wipe(i, end)
            i = end
            continue
        if not ch.isspace():
            line_start = False
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            wipe(i, end)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2) + 2
            wipe(i, end)
            i = end
        elif text.startswith('R"', i) and (i == 0 or not is_ident(text[i - 1])):
            delimiter = text[i + 2 : text.index("(", i)]
            end = text.index(")" + delimiter + '"', i) + len(delimiter) + 2
            wipe(i + 1, end)
            i = end
        elif ch in "\"'":
            if ch == "'" and i > 0 and text[i - 1].isalnum():  # a digit separator: 1'000
                i += 1
                continue
            end = i + 1
            while text[end] != ch:
                end += 2 if text[end] == "\\" else 1
            wipe(i + 1, end)
            i = end + 1
        else:
            i += 1
    blanked = "".join(out)
    return re.sub(r"(?m)^[ \t]*DREAMOR_[A-Z_]+[ \t]*$", lambda m: " " * len(m.group(0)), blanked)


def is_ident(ch: str) -> bool:
    return ch.isalnum() or ch == "_"


def angle_opens(text: str, i: int) -> bool:
    """Whether the < at i opens a template's arguments: after a name, spaced or not, or after the keyword template.
    Some styles space it (`enable_if_t <`); a < taken so that was a comparison is closed again by match() at the
    ) or ; that ends its group."""
    before = text[:i].rstrip()
    after_name = bool(before) and is_ident(before[-1]) and not before.endswith(("return", "operator"))
    return after_name or before.endswith("template")


def operator_end(text: str, i: int) -> int:
    """The end of an operator function's name, from just after the keyword operator."""
    j = i
    while j < len(text) and text[j] == " ":
        j += 1
    if text.startswith("()", j) or text.startswith("[]", j):
        return j + 2
    if j < len(text) and is_ident(text[j]):
        return i  # a conversion function or operator new: the keyword alone here
    while j < len(text) and text[j] in "+-*/%^&|~!=<>,":
        j += 1
    return j


def is_operator(text: str, i: int) -> bool:
    return text.startswith("operator", i) and (i == 0 or not is_ident(text[i - 1]))


def top_level(statement: str) -> str:
    """The statement with what is inside (), [], {} and template <> blanked, the brackets kept; an operator's name
    keeps its length, its () or [] written @@."""
    out, depth = [], []
    i = 0
    while i < len(statement):
        ch = statement[i]
        if is_operator(statement, i):
            end = operator_end(statement, i + len("operator"))
            name = "operator" + re.sub(r"[^\w ]", "@", statement[i + len("operator") : end])
            out.append(name if not depth else " " * len(name))
            i = end
            continue
        if ch in "([{" or (ch == "<" and angle_opens(statement, i)):
            depth.append(ch)
            out.append(ch if len(depth) == 1 else " ")
        elif depth and (ch in ")]}" or (ch == ">" and depth[-1] == "<" and statement[i - 1] != "-")):
            depth.pop()
            out.append(ch if not depth else " ")
        else:
            out.append(ch if not depth else " ")
        i += 1
    return "".join(out)


def without_requires(level: str) -> str:
    """A top-level statement with its parenthesized requires-clauses blanked: requires(...) before a class."""
    return re.sub(r"\brequires\s*\([^()]*\)", lambda m: " " * len(m.group(0)), level)


def first_top_level_assign(level: str) -> int:
    """The offset of the first = that is not part of ==, !=, <= or >=; -1 if none."""
    for match in re.finditer(r"=", level):
        k = match.start()
        if level[k - 1 : k] in ("=", "!", "<", ">") or level[k + 1 : k + 2] == "=":
            continue
        return k
    return -1


def strip_template_header(statement: str) -> tuple[str, str]:
    """(the template <...> lines, the rest)."""
    rest, header = statement.lstrip(), ""
    while rest.startswith("template"):
        level = top_level(rest)
        close = level.find(">", len("template"))
        if close < 0:
            break
        header += rest[: close + 1] + "\n"
        rest = rest[close + 1 :].lstrip()
    return header, rest


@dataclasses.dataclass
class Doc:
    line: int  # its first /// line
    lines: list[str]

    def has(self, marker: str) -> bool:
        return any(line == marker or line.startswith(marker + " ") for line in self.lines)

    def value(self, marker: str) -> str:
        return next((line[len(marker) :].strip() for line in self.lines if line.startswith(marker)), "")


@dataclasses.dataclass
class Scope:
    kind: str  # namespace, class, block
    names: list[str]  # the qualified name's parts
    public: bool  # whether what it declares is a user's
    access: str = "public"


@dataclasses.dataclass
class Decl:
    path: Path
    line: int
    kind: str
    owner: str  # the qualified name of the class it is a member of, or ""
    scope: str  # the qualified name its entry goes under
    name: str
    signature: str
    doc: Doc | None
    public: bool
    where: str = ""  # its qualified name in the code


@dataclasses.dataclass
class Statement:
    """One declaration as the scanner cuts it: where it is, the /// block above it, and its text - the template header,
    the rest, and the rest with what is inside its brackets blanked (top_level)."""

    scanner: "Scanner"
    scope: Scope
    start: int
    end: int
    line: int
    doc: Doc | None
    header: str
    rest: str
    level: str
    words: list[str]
    body: bool

    @property
    def in_class(self) -> bool:
        return self.scope.kind == "class"

    @property
    def visible(self) -> bool:
        return self.scope.public and self.scope.access == "public"

    def record(self, kind: str, name: str, signature: str) -> None:
        scanner = self.scanner
        where = "::".join(self.scope.names + [name])
        signature = scanner.with_literals(signature, self.start, self.end)
        owner = "::".join(self.scope.names) if self.in_class else ""
        entry_scope = "::".join(self.scope.names)
        scanner.decls.append(
            Decl(scanner.path, self.line, kind, owner, entry_scope, name, signature, self.doc, self.visible, where)
        )

    def skip(self, what: str) -> None:
        if self.doc and self.visible:
            self.scanner.problems.add(self.scanner.path, self.doc.line, f"a /// block on {what}, which has no entry")


class Scanner:
    """The declarations of one header at namespace and class scope, each with the /// block above it."""

    def __init__(self, path: Path, problems: Problems):
        self.path = path
        self.problems = problems
        self.text = path.read_text(encoding="utf-8")
        self.code = blank(self.text)
        self.line_starts = [0] + [m.end() for m in re.finditer("\n", self.text)]
        self.loose: list[Doc] = []
        self.docs = self.read_docs()
        self.decls: list[Decl] = []
        self.macros()
        self.scan()

    def line_of(self, offset: int) -> int:
        lo, hi = 0, len(self.line_starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self.line_starts[mid] <= offset:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1

    def read_docs(self) -> dict[int, Doc]:
        """Each /// block by the line the declaration below it starts on; a block with a blank line or nothing below
        it is loose (@section, @contract). Only a NOLINT line may stand between a block and its declaration, as it
        must be right above the line it excuses: any other // comment goes into the declaration's body, or above the
        block as a paragraph of its own."""
        lines = self.text.split("\n")
        docs: dict[int, Doc] = {}
        i = 0
        while i < len(lines):
            if not lines[i].lstrip().startswith("///"):
                i += 1
                continue
            start, block = i, []
            while i < len(lines) and lines[i].lstrip().startswith("///"):
                block.append(lines[i].lstrip()[3:].removeprefix(" ").rstrip())
                i += 1
            j = i
            while j < len(lines) and lines[j].lstrip().startswith("//"):
                if not lines[j].lstrip().startswith("// NOLINT"):
                    self.problems.add(
                        self.path,
                        j + 1,
                        "a // comment between a /// block and its declaration: put it in the body, or above the block",
                    )
                j += 1
            doc = Doc(start + 1, block)
            if j < len(lines) and lines[j].strip():
                docs[j + 1] = doc
            else:
                self.loose.append(doc)
        return docs

    def macros(self) -> None:
        seen = set()
        for match in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(DREAMOR_\w+)(\([^)]*\))?", self.text, re.M):
            name, line = match.group(1), self.line_of(match.start())
            doc = self.docs.pop(line, None)
            if name in seen or name.startswith("DREAMOR_INTERNAL_"):
                if doc:
                    self.problems.add(self.path, doc.line, f"a /// block on {name}: only a macro's first definition")
                continue
            seen.add(name)
            signature = f"#define {name}{match.group(2) or ''}"
            self.decls.append(Decl(self.path, line, "macro", "", "", name, signature, doc, True))

    # The scan keeps a stack of scopes. At namespace or class scope it reads statements: each ends at a ; or at the
    # { of a body, and every bracket inside one is skipped whole. A body is a namespace's or a class's, whose
    # statements are read in turn, or any other - a function's, an enum's - skipped whole.

    def scan(self) -> None:
        code, n = self.code, len(self.code)
        stack = [Scope("namespace", [], False)]
        i = start = 0
        while i < n:
            scope = stack[-1]
            ch = code[i]
            if ch == "}":
                stack.pop()
                i += 1
                start = i
            elif ch == ";":
                self.statement(scope, start, i, body=False)
                i += 1
                start = i
            elif ch == ":" and scope.kind == "class" and code[start:i].strip() in ("public", "private", "protected"):
                scope.access = code[start:i].strip()
                i += 1
                start = i
            elif is_operator(code, i):
                i = operator_end(code, i + len("operator"))
            elif ch == "{" and self.opens_body(start, i):
                opened = self.statement(scope, start, i, body=True)
                if isinstance(opened, Scope):
                    stack.append(opened)
                    i += 1
                else:
                    i = self.skip_block(i + 1)
                start = i
            elif ch in "([{" or (ch == "<" and angle_opens(code, i)):
                i = self.match(i)
            else:
                i += 1

    def opens_body(self, start: int, i: int) -> bool:
        """Whether the { at i opens a body rather than a braced initializer or a requires-expression."""
        statement = self.code[start:i]
        level = top_level(statement)
        words = IDENT.findall(level)
        if first_top_level_assign(level) >= 0:
            return False
        if words[:1] == ["namespace"] or words[:2] == ["inline", "namespace"] or "enum" in words:
            return True
        if any(key in words for key in CLASS_KEYS) and "(" not in without_requires(level):
            return True
        stripped = statement.rstrip()
        # Read on the top level, where a parenthesized group is one pair: requires(...) followed by a declaration is a
        # requires-clause, not a requires-expression.
        expression = re.search(r"(^|[^\w])requires\s*(\([^()]*\))?\s*$", level.rstrip())
        clause = re.search(r"(\)|>|\bconst|\bnoexcept|&)\s*requires\s*$", level.rstrip())
        if expression and not clause:
            return False
        if "(" not in level:
            return False
        after = level[level.rfind(")") + 1 :]
        if re.search(r"(?<!:):(?!:)", after) and stripped and (is_ident(stripped[-1]) or stripped[-1] == ">"):
            return False  # a member's braced initializer in a constructor's list
        return True

    def match(self, i: int) -> int:
        """The offset just after the bracket that closes the one at i."""
        code, depth = self.code, []
        while i < len(code):
            ch = code[i]
            if is_operator(code, i):
                i = operator_end(code, i + len("operator"))
                continue
            if ch in "([{" or (ch == "<" and angle_opens(code, i)):
                depth.append(ch)
            elif ch in ")]}" or (ch == ">" and depth and depth[-1] == "<" and code[i - 1] != "-"):
                while ch != ">" and depth and depth[-1] == "<":
                    depth.pop()  # a < that was a comparison
                if depth:
                    depth.pop()
                if not depth:
                    return i + 1
            elif ch == ";" and depth and depth[-1] == "<":
                depth.pop()
                if not depth:
                    return i
            i += 1
        return i

    def skip_block(self, i: int) -> int:
        """From just inside a body, the offset just after the brace that closes it."""
        depth = 1
        while depth:
            depth += (self.code[i] == "{") - (self.code[i] == "}")
            i += 1
        return i

    def with_literals(self, signature: str, start: int, end: int) -> str:
        """The signature with its string and character literals as the source writes them: it is cut from the
        blanked code, where a literal is its quotes around spaces. The literals of code[start:end] fill them in
        order; a signature only drops text, never reorders it."""
        literals, i = [], start
        while i < end:
            ch = self.code[i]
            if ch in "\"'" and not (ch == "'" and i > 0 and self.code[i - 1].isalnum()):
                close = self.code.index(ch, i + 1)
                literals.append(self.text[i : close + 1])
                i = close + 1
            else:
                i += 1
        pending = iter(literals)

        def restore(match: re.Match[str]) -> str:
            literal = next(pending, None)
            return literal if literal is not None and len(literal) == len(match.group(0)) else match.group(0)

        return re.sub(r"\"[ ]*\"|'[ ]*'", restore, signature)

    def statement(self, scope: Scope, start: int, end: int, body: bool) -> Scope | None:
        """Records the declaration in code[start:end], ended by a ; or by the { of a body; for a namespace or a
        class, the scope its body opens."""
        raw = self.code[start:end]
        if not raw.strip():
            return None
        offset = start + len(raw) - len(raw.lstrip())
        line = self.line_of(offset)
        doc = self.docs.pop(line, None)
        header, rest = strip_template_header(raw.strip())
        rest = DROPPED.sub("", rest)
        level = top_level(rest)
        words = IDENT.findall(level)
        if not words:
            return None
        if words[0] == "namespace" or words[:2] == ["inline", "namespace"]:
            names = scope.names + [p.replace("inline", "").strip() for p in rest.split("namespace", 1)[1].split("::")]
            return Scope(
                "namespace", names, bool(names) and names[0] == "dreamor" and not PRIVATE_NAMESPACES & set(names)
            )
        st = Statement(self, scope, start, end, line, doc, header, rest, level, words, body)
        first = words[0]
        if first == "static_assert" or words[:2] == ["using", "namespace"]:
            return st.skip("a static_assert or a using-directive")
        if first == "friend" and not body:
            return st.skip("a friend declaration")
        if re.match(r"template\s*<\s*>", header):
            if not body:
                return st.skip("an explicit specialization")
            if any(k in words for k in CLASS_KEYS):
                st.skip("an explicit specialization")
                return Scope("class", [], False)
            return Scope("block", [], False)
        if first == "using" and re.match(r"using\s+(\w+)(<.*>)?::\1\s*$", rest, re.S):
            return st.skip("an inheriting constructor")
        if first == "using":
            name = re.match(r"using\s+([\w:]+)", rest).group(1).split("::")[-1]
            if st.in_class and name in STANDARD_MEMBER_TYPES:
                return st.skip(f"{name}, a member type the standard names")
            assign = first_top_level_assign(level)
            st.record("type alias", name, header + (rest[:assign].rstrip() if assign >= 0 else rest))
            return None
        if "concept" in words:
            name = words[words.index("concept") + 1]
            assign = first_top_level_assign(level)
            st.record("concept", name, header + rest[:assign].rstrip())
            return None
        if "enum" in words:
            head = re.split(r"(?<!:):(?!:)", rest)[0].rstrip()
            st.record("enum", IDENT.findall(head)[-1], header + head)
            return None
        key = next((w for w in words if w in CLASS_KEYS), None)
        if key and "(" not in without_requires(level)[: level.find(key)]:
            return self.class_statement(st, key)
        assign = first_top_level_assign(level)
        parens = [m.start() for m in re.finditer(r"\(", level)]
        if parens and (assign < 0 or parens[0] < assign):
            return self.function_statement(st, parens)
        cut = assign if assign >= 0 else (level.find("{") if "{" in level else len(rest))
        names = IDENT.findall(level[:cut])
        if names:
            kind = "constant" if {"constexpr", "const"} & set(words) else "variable"
            st.record(kind, names[-1], header + rest[:cut].rstrip())
        return None

    def class_statement(self, st: "Statement", key: str) -> Scope | None:
        after = st.level[st.level.index(key) + len(key) :]
        names = [w for w in IDENT.findall(after) if w not in ("final", "alignas")]
        if not st.body:
            if not st.rest.startswith("friend"):
                st.skip("a forward declaration")
            return None
        if not names or after.lstrip().startswith(("{", ":")):
            return Scope("class", st.scope.names + ["(anonymous)"], False)
        name = names[0]
        specialization = after[after.index(name) + len(name) :].lstrip().startswith("<")
        exposed = bool(st.doc and st.doc.has("@exposed"))
        hidden = bool(st.doc and st.doc.has("@hidden"))
        public = (st.visible and not specialization and not hidden) or exposed
        cls = Scope("class", st.scope.names + [name], public, "private" if key == "class" else "public")
        if specialization and not exposed:  # an exposed one is the class a model calls: the chain's
            st.skip("a specialization")
        elif exposed and st.visible:
            self.problems.add(self.path, st.doc.line, f"@exposed on {name}, which is public already")
        else:
            head = re.split(r"(?<!:):(?!:)", st.rest)[0].rstrip()
            st.record("class", name, st.header + head.replace(" final", ""))
        return cls

    def function_statement(self, st: "Statement", parens: list[int]) -> None:
        level, rest = st.level, st.rest
        name_at = None
        for paren in parens:
            before = re.search(r"(\boperator\b[^(]*?|~?[A-Za-z_]\w*)\s*$", level[:paren])
            if before and before.group(1) not in NOT_A_NAME:
                name_at = before
                break
        if name_at is None:
            return None
        name = " ".join(rest[name_at.start(1) : name_at.end(1)].split())
        if level[: name_at.start(1)].rstrip().endswith("::") or name.startswith("~"):
            return None  # a member defined outside its class, or a destructor
        if re.search(r"=\s*(default|delete)\s*$", rest.rstrip()):
            return None
        if not st.in_class and level[: name_at.start(1)].strip() == "" and "->" in level:
            return st.skip("a deduction guide")
        if re.search(r"(^|\s)friend\s", level[: name_at.start(1)]):  # after a requires-clause, if any
            kind, rest = "function", re.sub(r"\bfriend\s+", "", rest, count=1)
        elif st.in_class:
            # An explicit specialization's class has no name in its scope, and no entry.
            kind = "constructor" if st.scope.names and name == st.scope.names[-1] else "member function"
        else:
            kind = "function"
        st.record(kind, name, st.header + cut_initializers(rest))
        return None


def cut_initializers(rest: str) -> str:
    """A function's declaration without its constructor's initializer list."""
    level = top_level(rest)
    close = level.rfind(")")
    for match in re.finditer(r"(?<!:):(?!:)", level):
        if match.start() > close:
            return rest[: match.start()].rstrip()
    return rest.rstrip()
