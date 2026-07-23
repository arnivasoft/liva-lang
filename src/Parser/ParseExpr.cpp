#include "liva/Parser/Parser.h"

namespace liva {

// Grammar (Pattern AST — Faz A, extended Faz B Task 3/4/5/6):
//   pattern      := binding
//   binding      := IDENT '@' pattern            // TOP-LEVEL ONLY (!inParens)
//                 | orPattern
//   orPattern    := primary ( '|' primary )*     // >=1 '|' -> OrPattern
//   primary      := '_' | int-literal [ rangeSuffix ] | ident
//                 | ident '(' subs ')' | ident '.' ident [ '(' subs ')' ]
//                 | '(' pattern (',' pattern)+ ')'   // TuplePattern, >=2 elems
//   rangeSuffix  := ('..' | '..=') int-literal   // -> RangePattern
//   subs    := pattern (',' pattern)*
// Pattern Types Faz B, Task 6: a '(' reached at the START of
// parsePatternPrimary (never after an IDENT — that's EnumCasePattern's
// `Case(subs)` form, parsed further below inside the identifier branch)
// begins a TuplePattern. Nested tuples fall out of the grammar for free:
// each element is parsed via the FULL recursive parsePattern(/*inParens=*/
// true), so a nested `(a,b)` element recurses right back into this same
// branch. A single-element `(p)` is a parse error (err_pattern_tuple_single_
// element) — the grammar has no separate "parenthesized grouping" form, so
// silently accepting `(p)` as a 1-element tuple would produce an arm that
// can never match any real subject type (Sema's arity check would reject it
// universally); diagnosing at the parser rejects it uniformly instead.
// GRAMMAR DEVIATION (Task 5) from the plan's one-line `binding := IDENT '@'
// primary`: here `@`'s RHS is the full recursive `pattern` (i.e. an
// orPattern), not a single primary — so `n @ 1 | 2` binds `n` to the WHOLE
// `1|2` alternation (`BindingPattern(n, OrPattern(1,2))`) rather than parsing
// as `(n@1) | 2`. See BindingPattern's doc comment (Pattern.h) for the full
// rationale. The `@` lookahead only fires at the top level (`!inParens`):
// inside a Case(...) subslot, `IDENT '@'` is NOT recognized as a binding —
// the IDENT parses as a bare identifier/case pattern and the dangling '@'
// then fails to parse cleanly at the enclosing ')'/','/'=>' expectation
// (Task 5 scope: `@` inside Case(...) is out of scope).
// Negative int literals are the '-' punctuator immediately followed by an
// integer_literal token, collapsed into a single IntLiteralPattern (matches
// the legacy parser's blind whitespace-free token concatenation, which
// happened to already "support" this via string concatenation). The same
// negative-literal handling applies to a range's `hi` endpoint (`-5..=5`).

/// After parsing an int-literal pattern atom (`lo`), look ahead for a `..`
/// (exclusive) / `..=` (inclusive) range suffix (Pattern Types Faz B, Task
/// 3) and, if present, parse the second int-literal endpoint and wrap both
/// into a RangePattern. Otherwise returns `lo` unchanged (upcast to
/// Pattern) — the common case, so every existing plain-int-literal pattern
/// call site is unaffected. `inParens` is threaded through from the calling
/// parsePattern() so the malformed-input recovery below can stay paren-
/// depth-aware, mirroring parsePattern's own atFallbackStop rule.
std::unique_ptr<Pattern> Parser::maybeParseRangePattern(
    std::unique_ptr<IntLiteralPattern> lo, SourceLocation startLoc, bool inParens) {
    if (!check(TokenKind::dotdot) && !check(TokenKind::dotdotequal))
        return lo;
    bool inclusive = check(TokenKind::dotdotequal);
    advance(); // consume '..' or '..='

    auto hiStart = current_.getLocation();
    std::unique_ptr<IntLiteralPattern> hi;
    bool hiNegative = check(TokenKind::minus);
    if (hiNegative)
        advance();
    if (check(TokenKind::integer_literal)) {
        std::string digits = std::string(current_.getText());
        int64_t value = current_.getIntegerValue();
        advance();
        hi = hiNegative
                 ? std::make_unique<IntLiteralPattern>(-value, "-" + digits, rangeFrom(hiStart))
                 : std::make_unique<IntLiteralPattern>(value, digits, rangeFrom(hiStart));
    } else {
        // Grammar-violating: '..'/'..=' not followed by an int literal
        // (e.g. `1..x`). Diagnose and recover with a dummy 0 endpoint so the
        // enclosing arm list can resync (mirrors the '-' recovery below).
        diag_.report(current_.getLocation(), DiagID::err_expected_token,
                     "integer literal", std::string(current_.getText()));
        // REVIEW FIX: consume the offending token (unless it's already an
        // arm/subpattern terminator) so the caller's next `expect(...)`
        // (fat_arrow in parseMatchExpr, or ')'/',' when this is a Case(...)
        // subpattern) doesn't immediately cascade a second, misleading
        // diagnostic pointing at the exact same spot. Before this fix,
        // `1..x =>` reported err_expected_token TWICE: once here, once from
        // parseMatchExpr's expect(fat_arrow) still sitting on `x`.
        bool atStop = check(TokenKind::fat_arrow) || check(TokenKind::kw_where) ||
                      check(TokenKind::kw_if) || check(TokenKind::r_brace) ||
                      check(TokenKind::eof) ||
                      (inParens && (check(TokenKind::r_paren) || check(TokenKind::comma)));
        if (!atStop)
            advance();
        hi = std::make_unique<IntLiteralPattern>(0, "0", rangeFrom(hiStart));
    }

    return std::make_unique<RangePattern>(std::move(lo), std::move(hi), inclusive,
                                           rangeFrom(startLoc));
}

std::unique_ptr<Pattern> Parser::parsePattern(bool inParens) {
    auto startLoc = current_.getLocation();

    // Pattern Types Faz B, Task 5: `name @ pattern` — recognized via a
    // 2-token lookahead (IDENT immediately followed by '@'), TOP-LEVEL ONLY
    // (!inParens — see grammar note above). The recursive parsePattern(false)
    // call for `sub` deliberately does NOT thread `inParens` through: `sub`
    // is always the top of a fresh pattern, never itself inside a Case(...)
    // subslot's parens, regardless of what `inParens` was for the outer
    // binding (this only matters for the atFallbackStop recovery paths deep
    // inside parsePatternPrimary; a binding can only ever be reached here
    // when inParens is already false, so this is actually always `false`
    // either way — spelled out explicitly for clarity).
    if (!inParens && check(TokenKind::identifier) && peek().is(TokenKind::at)) {
        std::string name = std::string(current_.getText());
        advance(); // consume IDENT
        advance(); // consume '@'
        auto sub = parsePattern(/*inParens=*/false);
        return std::make_unique<BindingPattern>(std::move(name), std::move(sub),
                                                 rangeFrom(startLoc));
    }

    auto first = parsePatternPrimary(inParens);

    // Pattern Types Faz B, Task 4: `orPattern := primary ('|' primary)*`,
    // left-associative. Only the single-pipe `pipe` token counts — `pipe_pipe`
    // ("||", the closure/logical-or token) does NOT start an or-alternative,
    // so `1 || 2 =>` is left alone here and fails naturally at the caller's
    // subsequent `expect(fat_arrow)`/`expect(r_paren)`/`expect(comma)`.
    if (!check(TokenKind::pipe))
        return first;

    std::vector<std::unique_ptr<Pattern>> alternatives;
    alternatives.push_back(std::move(first));
    while (check(TokenKind::pipe)) {
        advance(); // consume '|'
        alternatives.push_back(parsePatternPrimary(inParens));
    }
    return std::make_unique<OrPattern>(std::move(alternatives), rangeFrom(startLoc));
}

std::unique_ptr<Pattern> Parser::parsePatternPrimary(bool inParens) {
    auto startLoc = current_.getLocation();

    // Stop set for the best-effort blind-consumption fallbacks below: an arm
    // terminator always stops; `)`/`,` at the current depth additionally
    // stop when parsing a subpattern, so a malformed subpattern can never
    // swallow its enclosing Case(...)'s closing paren or comma.
    auto atFallbackStop = [this, inParens]() {
        if (check(TokenKind::fat_arrow) || check(TokenKind::kw_where) ||
            check(TokenKind::kw_if) || check(TokenKind::r_brace) || check(TokenKind::eof))
            return true;
        if (inParens && (check(TokenKind::r_paren) || check(TokenKind::comma)))
            return true;
        return false;
    };

    // Wildcard: `_`
    if (check(TokenKind::underscore)) {
        advance();
        return std::make_unique<WildcardPattern>(rangeFrom(startLoc));
    }

    // Bool literal: `true` / `false` (Pattern Types Faz B, Task 2).
    if (check(TokenKind::bool_literal)) {
        bool value = (current_.getText() == "true");
        advance();
        return std::make_unique<BoolLiteralPattern>(value, rangeFrom(startLoc));
    }

    // String literal: `"GET"` (Pattern Types Faz B, Task 2). sourceText
    // keeps the raw spelling (incl. quotes) for toString()/getSpelling();
    // value is the unescaped content used for runtime comparison.
    if (check(TokenKind::string_literal)) {
        std::string sourceText = std::string(current_.getText());
        std::string value = current_.getStringValue();
        advance();
        return std::make_unique<StringLiteralPattern>(std::move(value), std::move(sourceText),
                                                        rangeFrom(startLoc));
    }

    // Float literal: `3.14` (Pattern Types Faz B, Task 2).
    if (check(TokenKind::float_literal)) {
        std::string text = std::string(current_.getText());
        double value = current_.getFloatValue();
        advance();
        return std::make_unique<FloatLiteralPattern>(value, std::move(text), rangeFrom(startLoc));
    }

    // Negative integer literal: `-` immediately followed by an int literal.
    if (check(TokenKind::minus)) {
        advance();
        if (check(TokenKind::integer_literal)) {
            std::string digits = std::string(current_.getText());
            int64_t value = current_.getIntegerValue();
            advance();
            auto lo = std::make_unique<IntLiteralPattern>(-value, "-" + digits,
                                                           rangeFrom(startLoc));
            return maybeParseRangePattern(std::move(lo), startLoc, inParens);
        }
        // Grammar-violating input (bare '-' not followed by a digit).
        // Faz A kept this behavior-preserving (silently dropped the '-' and
        // fell through to a plain identifier pattern below) because the
        // legacy string splitter had no way to error here either; Faz B
        // lifts that constraint — this is a proper parse error now.
        diag_.report(current_.getLocation(), DiagID::err_expected_token,
                     "integer literal", std::string(current_.getText()));
        // Recovery: keep consuming like the old best-effort loop (below)
        // instead of getting stuck, so the enclosing pattern/arm list can
        // resync.
    }

    // Non-negative integer literal.
    if (check(TokenKind::integer_literal)) {
        std::string text = std::string(current_.getText());
        int64_t value = current_.getIntegerValue();
        advance();
        auto lo = std::make_unique<IntLiteralPattern>(value, text, rangeFrom(startLoc));
        return maybeParseRangePattern(std::move(lo), startLoc, inParens);
    }

    // Tuple pattern: '(' pattern (',' pattern)+ ')' (Pattern Types Faz B,
    // Task 6). See the grammar comment above parsePattern for the full
    // rationale (nested-tuple-for-free via recursion, single-element error).
    if (check(TokenKind::l_paren)) {
        advance(); // consume '('
        std::vector<std::unique_ptr<Pattern>> elements;
        if (!check(TokenKind::r_paren)) {
            while (true) {
                elements.push_back(parsePattern(/*inParens=*/true));
                if (check(TokenKind::comma)) {
                    advance();
                    continue;
                }
                break;
            }
        }
        if (check(TokenKind::r_paren)) {
            advance();
        }
        if (elements.size() < 2) {
            diag_.report(startLoc, DiagID::err_pattern_tuple_single_element);
        }
        return std::make_unique<TuplePattern>(std::move(elements), rangeFrom(startLoc));
    }

    // Identifier-rooted forms: Ident | Ident(subs) | Ident.Ident[(subs)].
    if (check(TokenKind::identifier)) {
        std::string name = std::string(current_.getText());
        advance();

        std::string enumName;
        std::string caseName = name;
        bool isDotted = false;

        if (check(TokenKind::dot)) {
            advance();
            isDotted = true;
            enumName = name;
            if (check(TokenKind::identifier)) {
                caseName = std::string(current_.getText());
                advance();
            } else {
                // Grammar-violating: 'Ident.' not followed by an
                // identifier (e.g. `Foo.123 =>`). Not observed in the repo
                // (Task 0); best-effort: continue blind consumption from
                // here — INCLUDING the already-consumed "Name." prefix —
                // until the fallback stop set, exactly like the old flat
                // loop would have.
                std::string full = name + ".";
                while (!atFallbackStop()) {
                    full += std::string(current_.getText());
                    advance();
                }
                return std::make_unique<IdentifierPattern>(std::move(full), rangeFrom(startLoc));
            }
        }

        if (check(TokenKind::l_paren)) {
            advance();
            std::vector<std::unique_ptr<Pattern>> subs;
            if (!check(TokenKind::r_paren)) {
                while (true) {
                    subs.push_back(parsePattern(/*inParens=*/true));
                    if (check(TokenKind::comma)) {
                        advance();
                        continue;
                    }
                    break;
                }
            }
            if (check(TokenKind::r_paren)) {
                advance();
            }
            auto pat = std::make_unique<EnumCasePattern>(
                std::move(enumName), std::move(caseName), std::move(subs), rangeFrom(startLoc));
            pat->setHasParens(true);
            return pat;
        }

        if (isDotted) {
            auto pat = std::make_unique<EnumCasePattern>(
                std::move(enumName), std::move(caseName),
                std::vector<std::unique_ptr<Pattern>>{}, rangeFrom(startLoc));
            pat->setHasParens(false);
            return pat;
        }

        // Plain identifier: binding or unqualified no-payload case.
        return std::make_unique<IdentifierPattern>(std::move(name), rangeFrom(startLoc));
    }

    // Grammar-violating: none of the above (not observed in the repo per
    // Task 0). Best-effort: consume tokens exactly like the old blind loop
    // and wrap them in an IdentifierPattern rather than erroring. Depth-aware
    // via atFallbackStop(): if this triggers while parsing a Case(...)
    // subpattern, it stops at `)`/`,` too, instead of swallowing the
    // enclosing parens/comma.
    std::string fallback;
    while (!atFallbackStop()) {
        fallback += std::string(current_.getText());
        advance();
    }
    return std::make_unique<IdentifierPattern>(std::move(fallback), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseExpression() {
    auto expr = parsePrecedenceExpr(0);
    if (!expr) return nullptr;
    // Ternary operator: condition ? thenExpr : elseExpr (lowest precedence)
    if (check(TokenKind::question)) {
        auto startLoc = expr->getStartLoc();
        advance(); // consume ?
        auto thenExpr = parseExpression();
        if (!thenExpr) return nullptr;
        expect(TokenKind::colon);
        auto elseExpr = parseExpression();
        if (!elseExpr) return nullptr;
        return std::make_unique<TernaryExpr>(std::move(expr), std::move(thenExpr),
                                              std::move(elseExpr), rangeFrom(startLoc));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parsePrecedenceExpr(int minPrec) {
    auto left = parseUnaryExpr();
    if (!left)
        return nullptr;

    // Handle 'as' / 'as?' cast operator
    if (check(TokenKind::kw_as)) {
        auto startLoc = left->getStartLoc();
        advance(); // consume 'as'
        bool isOptional = false;
        if (check(TokenKind::question)) {
            advance(); // consume '?'
            isOptional = true;
        }
        auto targetType = parseType();
        left = std::make_unique<CastExpr>(std::move(left), std::move(targetType),
                                          rangeFrom(startLoc), isOptional);
    }

    // Handle 'is' type check operator
    if (check(TokenKind::kw_is)) {
        auto startLoc = left->getStartLoc();
        advance(); // consume 'is'
        auto targetType = parseType();
        left = std::make_unique<IsExpr>(std::move(left), std::move(targetType),
                                        rangeFrom(startLoc));
    }

    while (true) {
        int prec = getBinaryOpPrecedence(current_.getKind());
        if (prec < minPrec)
            break;

        auto op = getBinaryOp(current_.getKind());
        auto startLoc = left->getStartLoc();
        advance(); // consume operator

        // Right-associativity: use minPrec = prec for right-assoc ops
        // Left-associativity: use minPrec = prec + 1
        // `??` is right-associative (Swift/TS/C# convention): a ?? b ?? c
        // parses as a ?? (b ?? c) so every stage's LHS stays an Optional
        // (the coalesce result of the inner `b ?? c` is the same T as `a`'s
        // inner type would need to unwrap against), instead of the plain-T
        // result of `(a ?? b)` making the outer `?? c` a no-op fallback.
        int nextMinPrec = (op == BinaryExpr::Op::NilCoalesce) ? prec : prec + 1;
        auto right = parsePrecedenceExpr(nextMinPrec);
        if (!right)
            return nullptr;

        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right),
                                            rangeFrom(startLoc));
    }

    // Check for range operator (.. or ..=)
    if (check(TokenKind::dotdot) || check(TokenKind::dotdotequal)) {
        auto startLoc = left->getStartLoc();
        bool inclusive = check(TokenKind::dotdotequal);
        advance(); // consume .. or ..=
        auto right = parsePrecedenceExpr(0);
        if (!right)
            return nullptr;
        return std::make_unique<RangeExpr>(std::move(left), std::move(right),
                                           inclusive, rangeFrom(startLoc));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parseUnaryExpr() {
    auto startLoc = current_.getLocation();

    // Unary minus
    if (match(TokenKind::minus)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Negate, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // Logical not
    if (match(TokenKind::bang)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Not, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // Bitwise not
    if (match(TokenKind::tilde)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::BitNot, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // try expression
    if (check(TokenKind::kw_try)) {
        advance();
        auto operand = parseUnaryExpr();
        if (!operand) return nullptr;
        return std::make_unique<TryExpr>(std::move(operand), rangeFrom(startLoc));
    }

    // await expression
    if (check(TokenKind::kw_await)) {
        advance();
        auto operand = parseUnaryExpr();
        if (!operand) return nullptr;
        return std::make_unique<AwaitExpr>(std::move(operand), rangeFrom(startLoc));
    }

    // yield expression (generator)
    if (check(TokenKind::kw_yield)) {
        advance();
        auto value = parseUnaryExpr();
        if (!value) return nullptr;
        return std::make_unique<YieldExpr>(std::move(value), rangeFrom(startLoc));
    }

    // ref / ref mut
    if (check(TokenKind::kw_ref)) {
        advance();
        bool isMut = match(TokenKind::kw_mut);
        auto expr = parseUnaryExpr();
        if (!expr)
            return nullptr;
        return std::make_unique<RefExpr>(std::move(expr), isMut, rangeFrom(startLoc));
    }

    auto primary = parsePrimaryExpr();
    if (!primary)
        return nullptr;

    return parsePostfixExpr(std::move(primary));
}

std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
    auto startLoc = current_.getLocation();

    switch (current_.getKind()) {
    case TokenKind::integer_literal: {
        auto value = current_.getIntegerValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<IntegerLiteralExpr>(value, range);
    }

    case TokenKind::float_literal: {
        auto value = current_.getFloatValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<FloatLiteralExpr>(value, range);
    }

    case TokenKind::bool_literal: {
        bool value = (current_.getText() == "true");
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<BoolLiteralExpr>(value, range);
    }

    case TokenKind::string_literal: {
        auto value = current_.getStringValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<StringLiteralExpr>(std::move(value), range);
    }

    case TokenKind::string_interp_begin: {
        // "hello \(name) world" → ("hello " + toString(name)) + " world"
        auto text = current_.getStringValue();
        advance(); // consume string_interp_begin

        std::unique_ptr<Expr> result;
        if (!text.empty()) {
            result = std::make_unique<StringLiteralExpr>(text, rangeFrom(startLoc));
        }

        // Parse interpolated expression
        auto expr = parseExpression();
        // Wrap in toString() call
        auto toStrCallee = std::make_unique<IdentifierExpr>("toString", rangeFrom(startLoc));
        std::vector<std::unique_ptr<Expr>> toStrArgs;
        toStrArgs.push_back(std::move(expr));
        auto toStrCall = std::make_unique<CallExpr>(
            std::move(toStrCallee), std::move(toStrArgs), rangeFrom(startLoc));

        if (result) {
            result = std::make_unique<BinaryExpr>(
                BinaryExpr::Op::Add, std::move(result), std::move(toStrCall), rangeFrom(startLoc));
        } else {
            result = std::move(toStrCall);
        }

        // Handle mid segments
        while (current_.is(TokenKind::string_interp_mid)) {
            auto midText = current_.getStringValue();
            advance();

            if (!midText.empty()) {
                auto midLit = std::make_unique<StringLiteralExpr>(midText, rangeFrom(startLoc));
                result = std::make_unique<BinaryExpr>(
                    BinaryExpr::Op::Add, std::move(result), std::move(midLit), rangeFrom(startLoc));
            }

            expr = parseExpression();
            auto midCallee = std::make_unique<IdentifierExpr>("toString", rangeFrom(startLoc));
            std::vector<std::unique_ptr<Expr>> midArgs;
            midArgs.push_back(std::move(expr));
            auto midCall = std::make_unique<CallExpr>(
                std::move(midCallee), std::move(midArgs), rangeFrom(startLoc));
            result = std::make_unique<BinaryExpr>(
                BinaryExpr::Op::Add, std::move(result), std::move(midCall), rangeFrom(startLoc));
        }

        // Handle end segment
        if (current_.is(TokenKind::string_interp_end)) {
            auto endText = current_.getStringValue();
            advance();
            if (!endText.empty()) {
                auto endLit = std::make_unique<StringLiteralExpr>(endText, rangeFrom(startLoc));
                result = std::make_unique<BinaryExpr>(
                    BinaryExpr::Op::Add, std::move(result), std::move(endLit), rangeFrom(startLoc));
            }
        }

        return result;
    }

    case TokenKind::kw_nil: {
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<NilLiteralExpr>(range);
    }

    case TokenKind::kw_self: {
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<IdentifierExpr>("self", range);
    }

    case TokenKind::kw_super: {
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<IdentifierExpr>("super", range);
    }

    case TokenKind::identifier: {
        std::string name(current_.getText());
        advance();
        auto range = rangeFrom(startLoc);

        // Check for macro invocation: name!(args)
        if (check(TokenKind::bang) && peek().is(TokenKind::l_paren)) {
            return parseMacroInvokeExpr(std::move(name), startLoc);
        }

        // Turbofish: Name::<T, U> ... disambiguated via '::' before '<'.
        // Gives explicit generic type args for a subsequent .method() call,
        // or for a struct literal: Name::<T, U> { field: value, ... }.
        if (check(TokenKind::coloncolon) && peek().is(TokenKind::less)) {
            advance(); // consume ::
            advance(); // consume <
            std::vector<std::unique_ptr<TypeRepr>> typeArgs;
            if (!check(TokenKind::greater)) {
                do {
                    auto ty = parseType();
                    if (!ty) return nullptr;
                    typeArgs.push_back(std::move(ty));
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::greater);
            // Followed by `{` → explicit-typed struct literal.
            if (check(TokenKind::l_brace) && !name.empty() &&
                name[0] >= 'A' && name[0] <= 'Z') {
                auto lit = parseStructLiteral(name, startLoc);
                if (lit && lit->getKind() == ASTNode::NodeKind::StructLiteralExpr) {
                    auto *sl = static_cast<StructLiteralExpr *>(lit.get());
                    sl->setTypeArgs(std::move(typeArgs));
                }
                return lit;
            }
            auto ident = std::make_unique<IdentifierExpr>(std::move(name), rangeFrom(startLoc));
            ident->setTypeArgs(std::move(typeArgs));
            return ident;
        }

        // Plain generic invocation without turbofish:
        //   Stream<i64>{...}   or   Stream<i64>.from(arr)
        // The `<` here would otherwise parse as less-than. Disambiguate by
        // scanning forward token-by-token (saving lexer state) until the
        // matching `>` and peeking what follows. Generic context only when:
        //   - identifier starts with an uppercase letter (struct convention)
        //   - the closing `>` is followed by `{` (struct literal) or `.`
        //     (static method call). For comparisons (`Foo < x > y`) we
        //     restore and fall through to the operator parser.
        if (check(TokenKind::less) && !name.empty() &&
            name[0] >= 'A' && name[0] <= 'Z') {
            auto savedLex = lexer_.saveState();
            Token savedCur = current_;
            advance(); // consume <
            int depth = 1;
            bool ok = false;
            while (!check(TokenKind::eof)) {
                if (check(TokenKind::less)) {
                    depth++;
                } else if (check(TokenKind::greater)) {
                    if (--depth == 0) { ok = true; break; }
                } else if (check(TokenKind::semicolon) ||
                           check(TokenKind::l_brace) ||
                           check(TokenKind::r_brace) ||
                           check(TokenKind::r_paren) ||
                           check(TokenKind::r_bracket)) {
                    // Tokens that should never appear inside a type-arg list
                    // (except the closing `>` we're scanning for) — bail
                    // and treat the original `<` as a comparison.
                    break;
                }
                advance();
            }
            if (ok) {
                advance(); // consume the matching `>`
                bool isGeneric = check(TokenKind::l_brace) ||
                                 check(TokenKind::dot);
                lexer_.restoreState(savedLex);
                current_ = savedCur;
                if (isGeneric) {
                    advance(); // consume <
                    std::vector<std::unique_ptr<TypeRepr>> typeArgs;
                    if (!check(TokenKind::greater)) {
                        do {
                            auto ty = parseType();
                            if (!ty) return nullptr;
                            typeArgs.push_back(std::move(ty));
                        } while (match(TokenKind::comma));
                    }
                    expect(TokenKind::greater);
                    if (check(TokenKind::l_brace)) {
                        auto lit = parseStructLiteral(name, startLoc);
                        if (lit && lit->getKind() ==
                                ASTNode::NodeKind::StructLiteralExpr) {
                            auto *sl = static_cast<StructLiteralExpr *>(lit.get());
                            sl->setTypeArgs(std::move(typeArgs));
                        }
                        return lit;
                    }
                    // Static method call follows: leave as IdentifierExpr
                    // with type args; postfix parser handles the .method().
                    auto ident = std::make_unique<IdentifierExpr>(
                        std::move(name), rangeFrom(startLoc));
                    ident->setTypeArgs(std::move(typeArgs));
                    return ident;
                }
            } else {
                lexer_.restoreState(savedLex);
                current_ = savedCur;
            }
        }

        // Check for struct literal: Name { field: value, ... }
        // Convention: struct names start with uppercase letter
        if (check(TokenKind::l_brace) && !name.empty() &&
            name[0] >= 'A' && name[0] <= 'Z' && !suppressStructLit_) {
            return parseStructLiteral(name, startLoc);
        }

        return std::make_unique<IdentifierExpr>(std::move(name), range);
    }

    case TokenKind::l_paren: {
        advance(); // consume (
        // Inside parens we're shielded from `if cond { ... }` ambiguity, so
        // struct literals are unambiguously fine again.
        bool savedSuppress = suppressStructLit_;
        suppressStructLit_ = false;
        auto expr = parseExpression();
        if (!expr) {
            suppressStructLit_ = savedSuppress;
            return nullptr;
        }

        // Comma → tuple literal
        if (match(TokenKind::comma)) {
            std::vector<std::unique_ptr<Expr>> elements;
            elements.push_back(std::move(expr));
            if (!check(TokenKind::r_paren)) {
                do {
                    auto elem = parseExpression();
                    if (!elem) {
                        suppressStructLit_ = savedSuppress;
                        return nullptr;
                    }
                    elements.push_back(std::move(elem));
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::r_paren);
            suppressStructLit_ = savedSuppress;
            return std::make_unique<TupleLiteralExpr>(std::move(elements), rangeFrom(startLoc));
        }

        // No comma → grouping expression
        expect(TokenKind::r_paren);
        suppressStructLit_ = savedSuppress;
        return std::make_unique<GroupExpr>(std::move(expr), rangeFrom(startLoc));
    }

    case TokenKind::l_bracket: {
        return parseArrayLiteral();
    }

    case TokenKind::kw_match: {
        return parseMatchExpr();
    }

    case TokenKind::pipe:
    case TokenKind::pipe_pipe:
        return parseClosureExpr();

    case TokenKind::kw_func:
        return parseClosureExpr();

    case TokenKind::kw_comptime:
        return parseComptimeExpr();

    default:
        diag_.report(current_.getLocation(), DiagID::err_expected_expression);
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parsePostfixExpr(std::unique_ptr<Expr> base) {
    while (true) {
        if (check(TokenKind::l_paren)) {
            // Pattern Types Faz B, Task 6: don't extend `base` into a call
            // across a source-line gap while parsing a match arm's body (see
            // `suppressCallAcrossNewline_`'s doc comment in Parser.h) — this
            // is what stops `(a,b) => c + d` followed on the next line by
            // the NEXT arm's `(e,f) => ...` from being mis-parsed as
            // `c + d(e,f)`.
            if (suppressCallAcrossNewline_ && previous_.getLocation().line != current_.getLocation().line) {
                break;
            }
            base = parseCallExpr(std::move(base));
            // Check for trailing closure after call: apply(5) |x| { ... }
            if (base && base->getKind() == ASTNode::NodeKind::CallExpr &&
                (check(TokenKind::pipe) || check(TokenKind::pipe_pipe))) {
                auto *call = static_cast<CallExpr *>(base.get());
                auto trailingClosure = parseClosureExpr();
                if (trailingClosure) {
                    call->addArg(std::move(trailingClosure));
                }
            }
        } else if (check(TokenKind::dot)) {
            base = parseMemberExpr(std::move(base), false);
        } else if (check(TokenKind::question_dot)) {
            base = parseMemberExpr(std::move(base), true);
        } else if (check(TokenKind::l_bracket)) {
            base = parseIndexExpr(std::move(base));
        } else if (check(TokenKind::bang)) {
            auto startLoc = base->getStartLoc();
            advance();
            base = std::make_unique<UnwrapExpr>(std::move(base), rangeFrom(startLoc));
        } else if (check(TokenKind::question)) {
            // Postfix ? (Result propagation) vs ternary ? disambiguation
            // If next token can start an expression → leave for ternary
            auto next = peek();
            bool canStartExpr =
                next.is(TokenKind::identifier) ||
                next.is(TokenKind::integer_literal) ||
                next.is(TokenKind::float_literal) ||
                next.is(TokenKind::string_literal) ||
                next.is(TokenKind::char_literal) ||
                next.is(TokenKind::kw_true) || next.is(TokenKind::kw_false) ||
                next.is(TokenKind::kw_nil) ||
                next.is(TokenKind::minus) ||    // unary -
                next.is(TokenKind::tilde) ||    // unary ~
                next.is(TokenKind::kw_ref) ||
                next.is(TokenKind::kw_try) ||
                next.is(TokenKind::kw_await) ||
                next.is(TokenKind::kw_match) ||
                next.is(TokenKind::kw_if) ||
                next.is(TokenKind::kw_comptime);
            if (!canStartExpr) {
                auto startLoc = base->getStartLoc();
                advance(); // consume ?
                base = std::make_unique<TryExpr>(std::move(base), rangeFrom(startLoc));
            } else {
                break; // ternary → parseExpression() will handle
            }
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseCallExpr(std::unique_ptr<Expr> callee) {
    auto startLoc = callee->getStartLoc();
    expect(TokenKind::l_paren);

    // Inside call args we're shielded from the if/while-condition
    // ambiguity, so struct literals are unambiguously fine.
    bool savedSuppress = suppressStructLit_;
    suppressStructLit_ = false;
    std::vector<std::unique_ptr<Expr>> args;
    if (!check(TokenKind::r_paren)) {
        do {
            auto arg = parseExpression();
            if (!arg) {
                // Skip to next comma or closing paren to recover
                if (!skipToExprDelimiter(TokenKind::r_paren)) {
                    suppressStructLit_ = savedSuppress;
                    return nullptr;
                }
                if (check(TokenKind::r_paren)) break;
                if (match(TokenKind::comma)) continue;
                break;
            }
            args.push_back(std::move(arg));
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::r_paren);
    suppressStructLit_ = savedSuppress;

    return std::make_unique<CallExpr>(std::move(callee), std::move(args),
                                      rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseMemberExpr(std::unique_ptr<Expr> object,
                                               bool isOptionalChain) {
    auto startLoc = object->getStartLoc();
    if (isOptionalChain)
        expect(TokenKind::question_dot);
    else
        expect(TokenKind::dot);

    // Tuple element access: .0, .1, etc.
    if (check(TokenKind::integer_literal)) {
        auto idx = current_;
        advance();
        return std::make_unique<MemberExpr>(std::move(object),
            std::string(idx.getText()), rangeFrom(startLoc), isOptionalChain);
    }

    // Allow select keywords as member names (contextual usage):
    // init/deinit (super.init), test (struct method named test)
    if (check(TokenKind::kw_init) || check(TokenKind::kw_deinit) ||
        check(TokenKind::kw_test)) {
        auto member = current_;
        advance();
        return std::make_unique<MemberExpr>(std::move(object),
            std::string(member.getText()), rangeFrom(startLoc), isOptionalChain);
    }

    auto member = expect(TokenKind::identifier);

    auto memberExpr = std::make_unique<MemberExpr>(
        std::move(object), std::string(member.getText()),
        rangeFrom(startLoc), isOptionalChain);

    // Explicit method type args at the call site:
    //   s.map::<i64>(...) — turbofish, unambiguous
    //   s.map<i64>(...)   — plain, disambiguated by lookahead just like
    //                       `Stream<T>{...}` at the primary level.
    if (check(TokenKind::coloncolon) && peek().is(TokenKind::less)) {
        advance(); // ::
        advance(); // <
        std::vector<std::unique_ptr<TypeRepr>> typeArgs;
        if (!check(TokenKind::greater)) {
            do {
                auto ty = parseType();
                if (!ty) return nullptr;
                typeArgs.push_back(std::move(ty));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
        memberExpr->setTypeArgs(std::move(typeArgs));
    } else if (check(TokenKind::less)) {
        // Try plain `<...>` only if the matching `>` is followed by `(` —
        // method calls with explicit type args. Otherwise it's a comparison.
        auto savedLex = lexer_.saveState();
        Token savedCur = current_;
        advance(); // <
        int depth = 1;
        bool ok = false;
        while (!check(TokenKind::eof)) {
            if (check(TokenKind::less)) {
                depth++;
            } else if (check(TokenKind::greater)) {
                if (--depth == 0) { ok = true; break; }
            } else if (check(TokenKind::semicolon) ||
                       check(TokenKind::l_brace) ||
                       check(TokenKind::r_brace) ||
                       check(TokenKind::r_paren) ||
                       check(TokenKind::r_bracket)) {
                break;
            }
            advance();
        }
        if (ok) {
            advance(); // >
            bool isCall = check(TokenKind::l_paren);
            lexer_.restoreState(savedLex);
            current_ = savedCur;
            if (isCall) {
                advance(); // <
                std::vector<std::unique_ptr<TypeRepr>> typeArgs;
                if (!check(TokenKind::greater)) {
                    do {
                        auto ty = parseType();
                        if (!ty) return nullptr;
                        typeArgs.push_back(std::move(ty));
                    } while (match(TokenKind::comma));
                }
                expect(TokenKind::greater);
                memberExpr->setTypeArgs(std::move(typeArgs));
            }
        } else {
            lexer_.restoreState(savedLex);
            current_ = savedCur;
        }
    }

    return memberExpr;
}

std::unique_ptr<Expr> Parser::parseIndexExpr(std::unique_ptr<Expr> base) {
    auto startLoc = base->getStartLoc();
    expect(TokenKind::l_bracket);

    auto index = parseExpression();
    if (!index) {
        // Skip to closing bracket to recover
        skipToExprDelimiter(TokenKind::r_bracket);
        if (check(TokenKind::r_bracket)) advance();
        return nullptr;
    }

    expect(TokenKind::r_bracket);

    return std::make_unique<IndexExpr>(std::move(base), std::move(index),
                                       rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseStructLiteral(const std::string &name,
                                                  SourceLocation startLoc) {
    expect(TokenKind::l_brace);

    std::vector<StructLiteralExpr::FieldInit> fields;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;

        auto fieldName = expect(TokenKind::identifier);
        expect(TokenKind::colon);
        auto value = parseExpression();
        if (!value) {
            // Skip to next comma or closing brace to recover
            if (!skipToExprDelimiter(TokenKind::r_brace))
                return nullptr;
            if (check(TokenKind::r_brace)) break;
            if (match(TokenKind::comma)) continue;
            break;
        }

        fields.push_back({std::string(fieldName.getText()), std::move(value)});

        if (!check(TokenKind::r_brace)) {
            match(TokenKind::comma); // Optional trailing comma
        }
    }

    expect(TokenKind::r_brace);

    return std::make_unique<StructLiteralExpr>(name, std::move(fields), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseArrayLiteral() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::l_bracket);

    bool savedSuppress = suppressStructLit_;
    suppressStructLit_ = false;
    std::vector<std::unique_ptr<Expr>> elements;
    if (!check(TokenKind::r_bracket)) {
        do {
            auto elem = parseExpression();
            if (!elem) {
                if (!skipToExprDelimiter(TokenKind::r_bracket)) {
                    suppressStructLit_ = savedSuppress;
                    return nullptr;
                }
                if (check(TokenKind::r_bracket)) break;
                if (match(TokenKind::comma)) continue;
                break;
            }
            elements.push_back(std::move(elem));
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::r_bracket);
    suppressStructLit_ = savedSuppress;

    return std::make_unique<ArrayLiteralExpr>(std::move(elements), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseMatchExpr() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_match);

    auto subject = parseExpression();
    if (!subject)
        return nullptr;

    expect(TokenKind::l_brace);

    std::vector<MatchArm> arms;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        MatchArm arm;

        // Parse pattern into the structured Pattern AST.
        arm.patternNode = parsePattern();

        // Parse optional guard clause: where <expr> or if <expr>
        if (match(TokenKind::kw_where) || match(TokenKind::kw_if)) {
            arm.guard = parseExpression();
        }

        expect(TokenKind::fat_arrow);

        // Parse body expression. Pattern Types Faz B, Task 6: guard against
        // the body's trailing postfix chain swallowing the NEXT arm's
        // leading `(...)` (TuplePattern) as a call continuation across the
        // (unmarked) arm boundary — see `suppressCallAcrossNewline_`'s doc
        // comment in Parser.h.
        bool savedSuppressCallAcrossNewline = suppressCallAcrossNewline_;
        suppressCallAcrossNewline_ = true;
        arm.body = parseExpression();
        suppressCallAcrossNewline_ = savedSuppressCallAcrossNewline;
        if (!arm.body)
            return nullptr;

        arms.push_back(std::move(arm));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<MatchExpr>(std::move(subject), std::move(arms),
                                       rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseClosureExpr() {
    auto startLoc = current_.getLocation();

    std::vector<ClosureExpr::Param> params;

    if (check(TokenKind::kw_func)) {
        // func(params) -> RetType { body }   or   func(params) { body }
        advance(); // consume 'func'
        expect(TokenKind::l_paren);
        if (!check(TokenKind::r_paren)) {
            do {
                auto name = expect(TokenKind::identifier).getText();
                std::unique_ptr<TypeRepr> type;
                if (match(TokenKind::colon)) {
                    type = parseType();
                }
                ClosureExpr::Param p;
                p.name = std::string(name);
                p.type = std::move(type);
                params.push_back(std::move(p));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::r_paren);
    } else if (check(TokenKind::pipe_pipe)) {
        advance(); // consume ||
    } else {
        expect(TokenKind::pipe);
        if (!check(TokenKind::pipe)) {
            do {
                auto name = expect(TokenKind::identifier).getText();
                std::unique_ptr<TypeRepr> type;
                if (match(TokenKind::colon)) {
                    type = parseType();
                }
                ClosureExpr::Param p;
                p.name = std::string(name);
                p.type = std::move(type);
                params.push_back(std::move(p));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::pipe);
    }

    std::unique_ptr<TypeRepr> returnType;
    if (match(TokenKind::arrow)) {
        returnType = parseType();
    }

    auto body = parseBlock();

    return std::make_unique<ClosureExpr>(
        std::move(params), std::move(returnType),
        std::move(body), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseComptimeExpr() {
    auto startLoc = current_.getLocation();
    advance(); // consume 'comptime'
    auto body = parseBlock();
    if (!body) return nullptr;
    return std::make_unique<ComptimeExpr>(std::move(body), rangeFrom(startLoc));
}

std::vector<Token> Parser::collectBalancedTokens() {
    std::vector<Token> tokens;
    int parenDepth = 0;

    while (!check(TokenKind::eof)) {
        if (check(TokenKind::l_paren)) ++parenDepth;
        else if (check(TokenKind::r_paren)) {
            if (parenDepth == 0) break;
            --parenDepth;
        }
        tokens.push_back(current_);
        advance();
    }

    return tokens;
}

std::unique_ptr<Expr> Parser::parseMacroInvokeExpr(std::string name, SourceLocation startLoc) {
    advance(); // consume '!'
    expect(TokenKind::l_paren);

    auto argTokens = collectBalancedTokens();

    expect(TokenKind::r_paren);

    return std::make_unique<MacroInvokeExpr>(std::move(name), std::move(argTokens),
                                              rangeFrom(startLoc));
}

} // namespace liva
