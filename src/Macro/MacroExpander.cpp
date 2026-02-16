#include "liva/Macro/MacroExpander.h"
#include "liva/Lexer/Lexer.h"
#include <sstream>

namespace liva {

void MacroExpander::registerMacro(const MacroDef &def) {
    macros_[def.name] = def;
}

bool MacroExpander::hasMacro(const std::string &name) const {
    return macros_.count(name) > 0;
}

std::string MacroExpander::expand(const std::string &name,
                                   const std::vector<Token> &argTokens,
                                   DiagnosticsEngine &diag,
                                   SourceLocation invokeLoc) const {
    auto it = macros_.find(name);
    if (it == macros_.end()) {
        diag.report(invokeLoc, DiagID::err_macro_undefined, name);
        return "";
    }

    const auto &def = it->second;

    // Emit Invocation trace event
    if (traceCallback_) {
        MacroTraceEvent ev;
        ev.phase = MacroTraceEvent::Invocation;
        ev.macroName = name;
        ev.invokeLoc = invokeLoc;
        traceCallback_(ev);
    }

    for (size_t armIdx = 0; armIdx < def.arms.size(); ++armIdx) {
        const auto &arm = def.arms[armIdx];
        std::map<std::string, MacroFragment> captures;
        std::map<std::string, std::vector<MacroFragment>> repCaptures;

        if (matchArm(arm, argTokens, captures, repCaptures)) {
            // Emit ArmMatched trace event
            if (traceCallback_) {
                MacroTraceEvent ev;
                ev.phase = MacroTraceEvent::ArmMatched;
                ev.macroName = name;
                ev.invokeLoc = invokeLoc;
                ev.armIndex = armIdx;
                for (const auto &[k, v] : captures)
                    ev.captures[k] = tokensToString(v.tokens);
                for (const auto &[k, v] : repCaptures)
                    ev.repCaptures[k] = v.size();
                traceCallback_(ev);
            }

            std::string result = substituteArm(arm, captures, repCaptures);

            // Emit Completed trace event
            if (traceCallback_) {
                MacroTraceEvent ev;
                ev.phase = MacroTraceEvent::Completed;
                ev.macroName = name;
                ev.invokeLoc = invokeLoc;
                ev.expandedSource = result;
                traceCallback_(ev);
            }

            return result;
        } else {
            // Emit ArmFailed trace event
            if (traceCallback_) {
                MacroTraceEvent ev;
                ev.phase = MacroTraceEvent::ArmFailed;
                ev.macroName = name;
                ev.invokeLoc = invokeLoc;
                ev.armIndex = armIdx;
                traceCallback_(ev);
            }
        }
    }

    // Emit NoMatch trace event
    if (traceCallback_) {
        MacroTraceEvent ev;
        ev.phase = MacroTraceEvent::NoMatch;
        ev.macroName = name;
        ev.invokeLoc = invokeLoc;
        traceCallback_(ev);
    }

    diag.report(invokeLoc, DiagID::err_macro_no_matching_arm, name);
    return "";
}

bool MacroExpander::captureFragment(const std::string &fragSpec,
                                     const std::vector<Token> &tokens,
                                     size_t &pos,
                                     MacroFragment &out) const {
    if (pos >= tokens.size())
        return false;

    if (fragSpec == "ident") {
        if (tokens[pos].is(TokenKind::identifier) || tokens[pos].isKeyword()) {
            out.tokens.push_back(tokens[pos]);
            ++pos;
            return true;
        }
        return false;
    }

    if (fragSpec == "literal") {
        auto k = tokens[pos].getKind();
        if (k == TokenKind::integer_literal || k == TokenKind::float_literal ||
            k == TokenKind::string_literal || k == TokenKind::bool_literal ||
            k == TokenKind::char_literal) {
            out.tokens.push_back(tokens[pos]);
            ++pos;
            return true;
        }
        return false;
    }

    if (fragSpec == "block") {
        if (!tokens[pos].is(TokenKind::l_brace))
            return false;
        int depth = 0;
        while (pos < tokens.size()) {
            if (tokens[pos].is(TokenKind::l_brace))
                ++depth;
            else if (tokens[pos].is(TokenKind::r_brace))
                --depth;
            out.tokens.push_back(tokens[pos]);
            ++pos;
            if (depth == 0)
                return true;
        }
        return false;
    }

    // Default: "expr" — balanced token sequence until comma or end
    // Capture tokens respecting balanced parens/brackets/braces
    int parenDepth = 0, braceDepth = 0, bracketDepth = 0;
    while (pos < tokens.size()) {
        auto k = tokens[pos].getKind();

        // At top level (not nested), comma and r_paren are delimiters
        if (parenDepth == 0 && braceDepth == 0 && bracketDepth == 0) {
            if (k == TokenKind::comma || k == TokenKind::r_paren)
                break;
        }

        if (k == TokenKind::l_paren) ++parenDepth;
        else if (k == TokenKind::r_paren) --parenDepth;
        else if (k == TokenKind::l_brace) ++braceDepth;
        else if (k == TokenKind::r_brace) --braceDepth;
        else if (k == TokenKind::l_bracket) ++bracketDepth;
        else if (k == TokenKind::r_bracket) --bracketDepth;

        out.tokens.push_back(tokens[pos]);
        ++pos;
    }

    return !out.tokens.empty();
}

bool MacroExpander::matchArm(const MacroArm &arm,
                              const std::vector<Token> &tokens,
                              std::map<std::string, MacroFragment> &captures,
                              std::map<std::string, std::vector<MacroFragment>> &repCaptures) const {
    size_t tokPos = 0;
    size_t patPos = 0;
    const auto &pat = arm.pattern;

    while (patPos < pat.size()) {
        const auto &mt = pat[patPos];

        if (mt.kind == MacroToken::Variable) {
            MacroFragment frag;
            if (!captureFragment(mt.fragSpec, tokens, tokPos, frag))
                return false;
            captures[mt.varName] = std::move(frag);
            ++patPos;
        } else if (mt.kind == MacroToken::RepStart) {
            // Repetition: RepStart, then Variable(s), then RepEnd
            // Collect the pattern tokens between RepStart and RepEnd
            ++patPos;
            std::vector<const MacroToken *> repPatternVars;
            while (patPos < pat.size() && pat[patPos].kind != MacroToken::RepEnd) {
                repPatternVars.push_back(&pat[patPos]);
                ++patPos;
            }

            char separator = ',';
            bool oneOrMore = false;
            if (patPos < pat.size() && pat[patPos].kind == MacroToken::RepEnd) {
                separator = pat[patPos].repSeparator;
                oneOrMore = pat[patPos].repOneOrMore;
                ++patPos;
            }

            // Initialize repCaptures for each variable
            for (auto *rv : repPatternVars) {
                if (rv->kind == MacroToken::Variable) {
                    repCaptures[rv->varName] = std::vector<MacroFragment>();
                }
            }

            // Match repetitions
            int repCount = 0;
            while (tokPos < tokens.size()) {
                // Try capturing each variable in the rep pattern
                bool matched = true;
                std::map<std::string, MacroFragment> tempCaptures;
                size_t savedPos = tokPos;

                for (auto *rv : repPatternVars) {
                    if (rv->kind == MacroToken::Variable) {
                        MacroFragment frag;
                        if (!captureFragment(rv->fragSpec, tokens, tokPos, frag)) {
                            matched = false;
                            break;
                        }
                        tempCaptures[rv->varName] = std::move(frag);
                    } else if (rv->kind == MacroToken::Regular) {
                        if (tokPos >= tokens.size() ||
                            tokens[tokPos].getKind() != rv->token.getKind()) {
                            matched = false;
                            break;
                        }
                        ++tokPos;
                    }
                }

                if (!matched) {
                    tokPos = savedPos;
                    break;
                }

                // Store captures
                for (auto &[name, frag] : tempCaptures) {
                    repCaptures[name].push_back(std::move(frag));
                }
                ++repCount;

                // Check for separator
                if (tokPos < tokens.size()) {
                    if (separator == ',' && tokens[tokPos].is(TokenKind::comma)) {
                        ++tokPos;
                    } else {
                        break;
                    }
                }
            }

            if (oneOrMore && repCount == 0)
                return false;
        } else if (mt.kind == MacroToken::Regular) {
            // Exact token match
            if (tokPos >= tokens.size())
                return false;
            if (tokens[tokPos].getKind() != mt.token.getKind())
                return false;
            // For identifiers, also match text (use ownedText to avoid dangling string_view)
            if (mt.token.is(TokenKind::identifier)) {
                const std::string &patText = mt.ownedText;
                if (std::string(tokens[tokPos].getText()) != patText)
                    return false;
            }
            ++tokPos;
            ++patPos;
        } else {
            ++patPos;
        }
    }

    // All pattern tokens consumed; all input tokens should be consumed too
    return tokPos >= tokens.size();
}

std::string MacroExpander::tokenToString(const Token &tok) const {
    auto k = tok.getKind();
    if (k == TokenKind::string_literal) {
        return "\"" + std::string(tok.getStringValue()) + "\"";
    }
    // For identifiers and literals, use the token text (from actual source)
    if (k == TokenKind::identifier || k == TokenKind::integer_literal ||
        k == TokenKind::float_literal || k == TokenKind::char_literal ||
        k == TokenKind::bool_literal) {
        return std::string(tok.getText());
    }
    // For keywords and punctuators, use static spelling (avoids dangling string_view
    // from parseMacroDef's local SourceManager)
    const char *spelling = getTokenSpelling(k);
    if (spelling && spelling[0] != '\0') {
        return std::string(spelling);
    }
    return std::string(tok.getText());
}

std::string MacroExpander::tokensToString(const std::vector<Token> &tokens) const {
    std::string result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0)
            result += " ";
        result += tokenToString(tokens[i]);
    }
    return result;
}

std::string MacroExpander::substituteArm(
    const MacroArm &arm,
    const std::map<std::string, MacroFragment> &captures,
    const std::map<std::string, std::vector<MacroFragment>> &repCaptures) const {

    std::string result;
    const auto &exp = arm.expansion;

    for (size_t i = 0; i < exp.size(); ++i) {
        const auto &mt = exp[i];

        if (mt.kind == MacroToken::Variable) {
            auto it = captures.find(mt.varName);
            if (it != captures.end()) {
                if (!result.empty() && result.back() != ' ' && result.back() != '(' &&
                    result.back() != '{')
                    result += " ";
                result += tokensToString(it->second.tokens);
            }
        } else if (mt.kind == MacroToken::RepStart) {
            // Collect expansion tokens until RepEnd
            ++i;
            std::vector<const MacroToken *> repExpTokens;
            while (i < exp.size() && exp[i].kind != MacroToken::RepEnd) {
                repExpTokens.push_back(&exp[i]);
                ++i;
            }

            // Determine repeat count from any rep capture variable
            size_t repCount = 0;
            for (auto *rt : repExpTokens) {
                if (rt->kind == MacroToken::Variable) {
                    auto rit = repCaptures.find(rt->varName);
                    if (rit != repCaptures.end()) {
                        repCount = rit->second.size();
                        break;
                    }
                }
            }

            // Expand for each repetition
            for (size_t r = 0; r < repCount; ++r) {
                if (r > 0)
                    result += " ";
                for (auto *rt : repExpTokens) {
                    if (rt->kind == MacroToken::Variable) {
                        auto rit = repCaptures.find(rt->varName);
                        if (rit != repCaptures.end() && r < rit->second.size()) {
                            if (!result.empty() && result.back() != ' ' &&
                                result.back() != '(' && result.back() != '{')
                                result += " ";
                            result += tokensToString(rit->second[r].tokens);
                        }
                    } else if (rt->kind == MacroToken::Regular) {
                        if (!result.empty() && result.back() != ' ' &&
                            result.back() != '(' && result.back() != '{')
                            result += " ";
                        result += rt->ownedText.empty() ? tokenToString(rt->token) : rt->ownedText;
                    }
                }
            }
        } else if (mt.kind == MacroToken::Regular) {
            if (!result.empty() && result.back() != ' ' && result.back() != '(' &&
                result.back() != '{' && mt.token.getKind() != TokenKind::r_paren &&
                mt.token.getKind() != TokenKind::r_brace &&
                mt.token.getKind() != TokenKind::r_bracket &&
                mt.token.getKind() != TokenKind::comma &&
                mt.token.getKind() != TokenKind::semicolon)
                result += " ";
            result += mt.ownedText.empty() ? tokenToString(mt.token) : mt.ownedText;
        }
    }

    return result;
}

// --- Static helper: parse raw macro source into MacroDef ---

MacroDef MacroExpander::parseMacroDef(const std::string &name, bool isPublic,
                                       const std::string &rawSource, SourceRange range) {
    MacroDef def;
    def.name = name;
    def.isPublic = isPublic;
    def.range = range;

    // Lex the raw source
    SourceManager sm("<macro:" + name + ">", rawSource);
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);

    std::vector<Token> allTokens;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.is(TokenKind::eof))
            break;
        allTokens.push_back(tok);
    }

    // Parse arms: (pattern) => { expansion }
    size_t pos = 0;
    while (pos < allTokens.size()) {
        // Skip leading commas between arms
        while (pos < allTokens.size() && allTokens[pos].is(TokenKind::comma))
            ++pos;

        if (pos >= allTokens.size())
            break;

        // Expect ( to start pattern
        if (!allTokens[pos].is(TokenKind::l_paren))
            break;
        ++pos;

        MacroArm arm;

        // Parse pattern tokens until matching )
        int depth = 1;
        while (pos < allTokens.size() && depth > 0) {
            if (allTokens[pos].is(TokenKind::l_paren))
                ++depth;
            else if (allTokens[pos].is(TokenKind::r_paren)) {
                --depth;
                if (depth == 0)
                    break;
            }

            // Check for $( ... ),* or $( ... ),+ repetition
            if (allTokens[pos].is(TokenKind::dollar) && pos + 1 < allTokens.size() &&
                allTokens[pos + 1].is(TokenKind::l_paren)) {
                MacroToken repStart;
                repStart.kind = MacroToken::RepStart;
                arm.pattern.push_back(repStart);
                pos += 2; // skip $, (

                // Parse inner pattern until )
                int innerDepth = 1;
                while (pos < allTokens.size() && innerDepth > 0) {
                    if (allTokens[pos].is(TokenKind::l_paren))
                        ++innerDepth;
                    else if (allTokens[pos].is(TokenKind::r_paren)) {
                        --innerDepth;
                        if (innerDepth == 0)
                            break;
                    }

                    // Check for $var:spec inside repetition
                    if (allTokens[pos].is(TokenKind::dollar) &&
                        pos + 1 < allTokens.size() &&
                        allTokens[pos + 1].is(TokenKind::identifier)) {
                        MacroToken var;
                        var.kind = MacroToken::Variable;
                        var.varName = std::string(allTokens[pos + 1].getText());
                        pos += 2;
                        if (pos < allTokens.size() && allTokens[pos].is(TokenKind::colon)) {
                            ++pos;
                            if (pos < allTokens.size() && allTokens[pos].is(TokenKind::identifier)) {
                                var.fragSpec = std::string(allTokens[pos].getText());
                                ++pos;
                            }
                        }
                        arm.pattern.push_back(var);
                    } else {
                        MacroToken reg;
                        reg.kind = MacroToken::Regular;
                        reg.token = allTokens[pos];
                        reg.ownedText = allTokens[pos].is(TokenKind::string_literal)
                            ? ("\"" + std::string(allTokens[pos].getStringValue()) + "\"")
                            : std::string(allTokens[pos].getText());
                        arm.pattern.push_back(reg);
                        ++pos;
                    }
                }
                if (pos < allTokens.size())
                    ++pos; // skip )

                // After ), expect separator and quantifier: ,* or ,+ or just * or +
                MacroToken repEnd;
                repEnd.kind = MacroToken::RepEnd;
                repEnd.repSeparator = ',';
                repEnd.repOneOrMore = false;

                if (pos < allTokens.size() && allTokens[pos].is(TokenKind::comma)) {
                    ++pos;
                }
                if (pos < allTokens.size()) {
                    if (allTokens[pos].is(TokenKind::star)) {
                        repEnd.repOneOrMore = false;
                        ++pos;
                    } else if (allTokens[pos].is(TokenKind::plus)) {
                        repEnd.repOneOrMore = true;
                        ++pos;
                    }
                }

                arm.pattern.push_back(repEnd);
            }
            // Check for $var:spec
            else if (allTokens[pos].is(TokenKind::dollar) &&
                     pos + 1 < allTokens.size() &&
                     allTokens[pos + 1].is(TokenKind::identifier)) {
                MacroToken var;
                var.kind = MacroToken::Variable;
                var.varName = std::string(allTokens[pos + 1].getText());
                pos += 2;
                if (pos < allTokens.size() && allTokens[pos].is(TokenKind::colon)) {
                    ++pos;
                    if (pos < allTokens.size() && allTokens[pos].is(TokenKind::identifier)) {
                        var.fragSpec = std::string(allTokens[pos].getText());
                        ++pos;
                    }
                }
                arm.pattern.push_back(var);
            }
            // Regular pattern token (including commas as literal separators)
            else {
                MacroToken reg;
                reg.kind = MacroToken::Regular;
                reg.token = allTokens[pos];
                reg.ownedText = allTokens[pos].is(TokenKind::string_literal)
                    ? ("\"" + std::string(allTokens[pos].getStringValue()) + "\"")
                    : std::string(allTokens[pos].getText());
                arm.pattern.push_back(reg);
                ++pos;
            }
        }
        if (pos < allTokens.size())
            ++pos; // skip )

        // Expect =>
        if (pos < allTokens.size() && allTokens[pos].is(TokenKind::fat_arrow))
            ++pos;

        // Expect { expansion }
        if (pos < allTokens.size() && allTokens[pos].is(TokenKind::l_brace)) {
            ++pos;
            int braceDepth = 1;

            while (pos < allTokens.size() && braceDepth > 0) {
                if (allTokens[pos].is(TokenKind::l_brace))
                    ++braceDepth;
                else if (allTokens[pos].is(TokenKind::r_brace)) {
                    --braceDepth;
                    if (braceDepth == 0)
                        break;
                }

                // Check for $( ... )+ repetition in expansion
                if (allTokens[pos].is(TokenKind::dollar) && pos + 1 < allTokens.size() &&
                    allTokens[pos + 1].is(TokenKind::l_paren)) {
                    MacroToken repStart;
                    repStart.kind = MacroToken::RepStart;
                    arm.expansion.push_back(repStart);
                    pos += 2; // skip $, (

                    int innerDepth = 1;
                    while (pos < allTokens.size() && innerDepth > 0) {
                        if (allTokens[pos].is(TokenKind::l_paren))
                            ++innerDepth;
                        else if (allTokens[pos].is(TokenKind::r_paren)) {
                            --innerDepth;
                            if (innerDepth == 0)
                                break;
                        }

                        if (allTokens[pos].is(TokenKind::dollar) &&
                            pos + 1 < allTokens.size() &&
                            allTokens[pos + 1].is(TokenKind::identifier)) {
                            MacroToken var;
                            var.kind = MacroToken::Variable;
                            var.varName = std::string(allTokens[pos + 1].getText());
                            pos += 2;
                            arm.expansion.push_back(var);
                        } else {
                            MacroToken reg;
                            reg.kind = MacroToken::Regular;
                            reg.token = allTokens[pos];
                            reg.ownedText = allTokens[pos].is(TokenKind::string_literal)
                                ? ("\"" + std::string(allTokens[pos].getStringValue()) + "\"")
                                : std::string(allTokens[pos].getText());
                            arm.expansion.push_back(reg);
                            ++pos;
                        }
                    }
                    if (pos < allTokens.size())
                        ++pos; // skip )

                    MacroToken repEnd;
                    repEnd.kind = MacroToken::RepEnd;
                    if (pos < allTokens.size() && allTokens[pos].is(TokenKind::plus)) {
                        repEnd.repOneOrMore = true;
                        ++pos;
                    } else if (pos < allTokens.size() && allTokens[pos].is(TokenKind::star)) {
                        repEnd.repOneOrMore = false;
                        ++pos;
                    }
                    arm.expansion.push_back(repEnd);
                }
                // Check for $var in expansion
                else if (allTokens[pos].is(TokenKind::dollar) &&
                         pos + 1 < allTokens.size() &&
                         allTokens[pos + 1].is(TokenKind::identifier)) {
                    MacroToken var;
                    var.kind = MacroToken::Variable;
                    var.varName = std::string(allTokens[pos + 1].getText());
                    pos += 2;
                    arm.expansion.push_back(var);
                }
                // Regular expansion token
                else {
                    MacroToken reg;
                    reg.kind = MacroToken::Regular;
                    reg.token = allTokens[pos];
                    reg.ownedText = allTokens[pos].is(TokenKind::string_literal)
                        ? ("\"" + std::string(allTokens[pos].getStringValue()) + "\"")
                        : std::string(allTokens[pos].getText());
                    arm.expansion.push_back(reg);
                    ++pos;
                }
            }
            if (pos < allTokens.size())
                ++pos; // skip }
        }

        def.arms.push_back(std::move(arm));
    }

    return def;
}

} // namespace liva
