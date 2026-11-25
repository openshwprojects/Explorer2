#pragma once
// BerkeleyEqnLogic.h — Berkeley SIS / ABC / Synopsys Boolean Equation Netlist (.eqn) Reader
// Parses `NAME = ...;`, `INORDER = ...;`, `OUTORDER = ...;`, and `<lhs> = <boolean_expr>;` statements
// via a true recursive-descent boolean expression parser supporting:
//   - OR:  `+`, `|`
//   - XOR: `^`
//   - AND: `*`, `&`, and implicit adjacency (`a b`, `(a+b)(c+d)`, `!a b'`)
//   - NOT: prefix `!` / `~` and postfix `'` (`a'`, `(a+b)'`)
//   - Constants `0`, `1` (`ZERO`, `ONE`) and nested parentheses `(...)`
// Synthesizes a multi-level `DetailGateAndDaqEda::GateNode` logic gate DAG with levelized placement &
// orthogonal net routing via `RenderGateDagToBoard`!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailBerkeleyEqn {

enum class TokKind : uint8_t {
    End = 0,
    Ident,
    Const0,
    Const1,
    LParen,
    RParen,
    PrefixNot,  // ! or ~
    PostfixNot, // '
    And,        // * or &
    Or,         // + or |
    Xor         // ^
};

struct EqnToken {
    TokKind kind = TokKind::End;
    std::string text;
};

inline bool TokenizeBoolExpr(std::string_view expr, std::vector<EqnToken>& outToks) {
    outToks.clear();
    size_t i = 0u;
    while (i < expr.size()) {
        const unsigned char uc = static_cast<unsigned char>(expr[i]);
        if (uc <= 0x20u) { ++i; continue; }
        const char c = expr[i];
        if (c == '(') { outToks.push_back({ TokKind::LParen, "(" }); ++i; continue; }
        if (c == ')') { outToks.push_back({ TokKind::RParen, ")" }); ++i; continue; }
        if (c == '!' || c == '~') { outToks.push_back({ TokKind::PrefixNot, "!" }); ++i; continue; }
        if (c == '\'') { outToks.push_back({ TokKind::PostfixNot, "'" }); ++i; continue; }
        if (c == '*' || c == '&') { outToks.push_back({ TokKind::And, "*" }); ++i; continue; }
        if (c == '+' || c == '|') { outToks.push_back({ TokKind::Or, "+" }); ++i; continue; }
        if (c == '^') { outToks.push_back({ TokKind::Xor, "^" }); ++i; continue; }

        // Identifier or constant (`[A-Za-z0-9_.\[\]/]+`)
        if (std::isalnum(uc) || c == '_' || c == '[' || c == '\\' || c == '.') {
            const size_t st = i;
            while (i < expr.size()) {
                const char ic = expr[i];
                if (std::isalnum(static_cast<unsigned char>(ic)) || ic == '_' || ic == '[' || ic == ']' || ic == '.') {
                    ++i;
                } else {
                    break;
                }
            }
            std::string id(expr.substr(st, i - st));
            if (id == "0" || ReaderText::AsciiEqCI(id, "ZERO") || ReaderText::AsciiEqCI(id, "FALSE")) {
                outToks.push_back({ TokKind::Const0, "0" });
            } else if (id == "1" || ReaderText::AsciiEqCI(id, "ONE") || ReaderText::AsciiEqCI(id, "TRUE")) {
                outToks.push_back({ TokKind::Const1, "1" });
            } else {
                outToks.push_back({ TokKind::Ident, std::move(id) });
            }
            continue;
        }
        return false; // Unexpected illegal character in `.eqn` expression
    }
    outToks.push_back({ TokKind::End, {} });
    return true;
}

class BoolExprSynth {
public:
    BoolExprSynth(
        const std::vector<EqnToken>& toks,
        std::vector<DetailGateAndDaqEda::GateNode>& nodes,
        std::set<std::string>& discoveredInputs,
        int& wireSeq)
        : toks_(toks), nodes_(nodes), inputs_(discoveredInputs), wireSeq_(wireSeq) {}

    bool ParseRoot(const std::string& targetNet) {
        std::string rootSig;
        if (!ParseOr(rootSig, 0)) return false;
        if (Peek().kind != TokKind::End) return false;

        // If `rootSig` was already an internal gate driving `_nK`, rename its output directly to `targetNet`;
        // otherwise (direct variable alias `y = a;` or constant `y = 1;`) insert a `BUF` gate!
        if (!nodes_.empty() && !nodes_.back().pins.empty() &&
            nodes_.back().pins.back().isOutput && nodes_.back().pins.back().netName == rootSig &&
            rootSig.rfind("_eqn_", 0) == 0)
        {
            nodes_.back().pins.back().netName = targetNet;
            nodes_.back().instName = targetNet + "_" + nodes_.back().cellType;
        } else {
            EmitGate("BUF", { rootSig }, targetNet);
        }
        return true;
    }

private:
    const std::vector<EqnToken>& toks_;
    size_t pos_ = 0u;
    std::vector<DetailGateAndDaqEda::GateNode>& nodes_;
    std::set<std::string>& inputs_;
    int& wireSeq_;

    const EqnToken& Peek() const { return toks_[std::min(pos_, toks_.size() - 1u)]; }
    const EqnToken& Advance() { return toks_[std::min(pos_++, toks_.size() - 1u)]; }

    std::string NewTempNet() {
        return "_eqn_" + std::to_string(++wireSeq_);
    }

    std::string EmitGate(const std::string& gateType, const std::vector<std::string>& ins, const std::string& outNet) {
        DetailGateAndDaqEda::GateNode g;
        g.instName = "G" + std::to_string(nodes_.size() + 1u);
        g.cellType = gateType;
        for (size_t i = 0; i < ins.size(); ++i) {
            g.pins.push_back({ "I" + std::to_string(i + 1u), ins[i], false });
        }
        g.pins.push_back({ "Y", outNet, true });
        if (nodes_.size() < 2048u) nodes_.push_back(std::move(g));
        return outNet;
    }

    bool CanStartImplicitFactor(TokKind k) const {
        return k == TokKind::Ident || k == TokKind::Const0 || k == TokKind::Const1 ||
               k == TokKind::LParen || k == TokKind::PrefixNot;
    }

    bool ParseOr(std::string& outNet, int depth) {
        if (depth > 64) return false;
        std::vector<std::string> terms;
        std::string first;
        if (!ParseXor(first, depth + 1)) return false;
        terms.push_back(std::move(first));
        while (Peek().kind == TokKind::Or) {
            Advance();
            std::string rhs;
            if (!ParseXor(rhs, depth + 1)) return false;
            terms.push_back(std::move(rhs));
        }
        if (terms.size() == 1u) {
            outNet = std::move(terms[0]);
        } else {
            outNet = EmitGate("OR" + std::to_string(terms.size()), terms, NewTempNet());
        }
        return true;
    }

    bool ParseXor(std::string& outNet, int depth) {
        if (depth > 64) return false;
        std::string acc;
        if (!ParseAnd(acc, depth + 1)) return false;
        while (Peek().kind == TokKind::Xor) {
            Advance();
            std::string rhs;
            if (!ParseAnd(rhs, depth + 1)) return false;
            acc = EmitGate("XOR2", { acc, rhs }, NewTempNet());
        }
        outNet = std::move(acc);
        return true;
    }

    bool ParseAnd(std::string& outNet, int depth) {
        if (depth > 64) return false;
        std::vector<std::string> factors;
        std::string first;
        if (!ParseUnary(first, depth + 1)) return false;
        factors.push_back(std::move(first));
        while (Peek().kind == TokKind::And || CanStartImplicitFactor(Peek().kind)) {
            if (Peek().kind == TokKind::And) Advance();
            std::string rhs;
            if (!ParseUnary(rhs, depth + 1)) return false;
            factors.push_back(std::move(rhs));
        }
        if (factors.size() == 1u) {
            outNet = std::move(factors[0]);
        } else {
            outNet = EmitGate("AND" + std::to_string(factors.size()), factors, NewTempNet());
        }
        return true;
    }

    bool ParseUnary(std::string& outNet, int depth) {
        if (depth > 64) return false;
        if (Peek().kind == TokKind::PrefixNot) {
            Advance();
            std::string inner;
            if (!ParseUnary(inner, depth + 1)) return false;
            outNet = EmitGate("INV", { inner }, NewTempNet());
            return true;
        }
        std::string base;
        if (!ParsePrimary(base, depth + 1)) return false;
        while (Peek().kind == TokKind::PostfixNot) {
            Advance();
            base = EmitGate("INV", { base }, NewTempNet());
        }
        outNet = std::move(base);
        return true;
    }

    bool ParsePrimary(std::string& outNet, int depth) {
        const EqnToken& tk = Peek();
        if (tk.kind == TokKind::Ident) {
            outNet = tk.text;
            inputs_.insert(outNet);
            Advance();
            return true;
        }
        if (tk.kind == TokKind::Const0 || tk.kind == TokKind::Const1) {
            const std::string cName = (tk.kind == TokKind::Const1) ? "CONST_1" : "CONST_0";
            inputs_.insert(cName);
            outNet = cName;
            Advance();
            return true;
        }
        if (tk.kind == TokKind::LParen) {
            Advance();
            if (!ParseOr(outNet, depth + 1)) return false;
            if (Peek().kind != TokKind::RParen) return false;
            Advance();
            return true;
        }
        return false;
    }
};

} // namespace DetailBerkeleyEqn

inline bool ParseBerkeleyEqnLogic(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailBerkeleyEqn;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 12u, (16u << 20))) return false;

    // Strip `#` line comments and join `;`-terminated statements
    std::string clean;
    clean.reserve(sv.size());
    {
        size_t pos = 0u;
        std::string_view rawLine;
        while (ReaderText::NextLineSv(sv, pos, rawLine)) {
            const size_t hp = rawLine.find('#');
            if (hp != std::string_view::npos) rawLine = rawLine.substr(0u, hp);
            // Support `\` line continuation if present
            std::string_view tr = ReaderText::TrimSv(rawLine);
            if (tr.empty()) continue;
            if (tr.back() == '\\') {
                clean.append(tr.substr(0u, tr.size() - 1u));
                clean.push_back(' ');
            } else {
                clean.append(tr);
                clean.push_back(' ');
            }
        }
    }
    if (clean.find('=') == std::string::npos || clean.find(';') == std::string::npos) return false;

    std::string modelName;
    std::vector<std::string> inOrder, outOrder;
    std::set<std::string> referencedVars;
    std::set<std::string> assignedNets;
    std::vector<DetailGateAndDaqEda::GateNode> gateNodes;
    int wireSeq = 0;
    size_t eqnCount = 0u;

    size_t sp = 0u;
    while (sp < clean.size()) {
        const size_t semi = clean.find(';', sp);
        if (semi == std::string::npos) {
            // Trailing non-whitespace without ';' is invalid in `.eqn`
            if (!ReaderText::TrimSv(std::string_view(clean).substr(sp)).empty()) return false;
            break;
        }
        std::string_view stmt = ReaderText::TrimSv(std::string_view(clean).substr(sp, semi - sp));
        sp = semi + 1u;
        if (stmt.empty()) continue;

        const size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) return false;
        std::string_view lhs = ReaderText::TrimSv(stmt.substr(0u, eq));
        std::string_view rhs = ReaderText::TrimSv(stmt.substr(eq + 1u));
        if (lhs.empty() || rhs.empty()) return false;

        if (ReaderText::AsciiEqCI(lhs, "NAME")) {
            modelName = std::string(rhs);
        } else if (ReaderText::AsciiEqCI(lhs, "INORDER")) {
            std::string_view rem = rhs, tk;
            while (ReaderText::NextTokenSv(rem, tk)) inOrder.emplace_back(tk);
        } else if (ReaderText::AsciiEqCI(lhs, "OUTORDER")) {
            std::string_view rem = rhs, tk;
            while (ReaderText::NextTokenSv(rem, tk)) outOrder.emplace_back(tk);
        } else {
            // Boolean assignment `<signal> = <expr>;`
            for (char c : lhs) {
                if (static_cast<unsigned char>(c) <= 0x20u) return false;
            }
            std::vector<EqnToken> toks;
            if (!TokenizeBoolExpr(rhs, toks)) return false;

            const std::string target(lhs);
            BoolExprSynth synth(toks, gateNodes, referencedVars, wireSeq);
            if (!synth.ParseRoot(target)) return false;
            assignedNets.insert(target);
            ++eqnCount;
        }
    }

    if (eqnCount == 0u || gateNodes.empty()) return false;

    // Build Primary Input & Primary Output `GateNode` chevrons around the synthesized gate DAG!
    std::vector<DetailGateAndDaqEda::GateNode> allNodes;
    std::set<std::string> addedPi;
    for (const auto& pi : inOrder) {
        if (addedPi.insert(pi).second) {
            DetailGateAndDaqEda::GateNode n;
            n.instName = pi;
            n.cellType = "PI";
            n.isPrimaryIn = true;
            n.pins.push_back({ "Y", pi, true });
            allNodes.push_back(std::move(n));
        }
    }
    for (const auto& rv : referencedVars) {
        if (assignedNets.count(rv) == 0u && addedPi.insert(rv).second) {
            DetailGateAndDaqEda::GateNode n;
            n.instName = rv;
            n.cellType = "PI";
            n.isPrimaryIn = true;
            n.pins.push_back({ "Y", rv, true });
            allNodes.push_back(std::move(n));
        }
    }

    for (auto& gn : gateNodes) allNodes.push_back(std::move(gn));

    std::set<std::string> addedPo;
    if (!outOrder.empty()) {
        for (const auto& po : outOrder) {
            if (addedPo.insert(po).second) {
                DetailGateAndDaqEda::GateNode n;
                n.instName = po;
                n.cellType = "PO";
                n.isPrimaryOut = true;
                n.pins.push_back({ "A", po, false });
                allNodes.push_back(std::move(n));
            }
        }
    } else {
        for (const auto& an : assignedNets) {
            if (referencedVars.count(an) == 0u && addedPo.insert(an).second) {
                DetailGateAndDaqEda::GateNode n;
                n.instName = an;
                n.cellType = "PO";
                n.isPrimaryOut = true;
                n.pins.push_back({ "A", an, false });
                allNodes.push_back(std::move(n));
            }
        }
        if (addedPo.empty()) {
            for (const auto& an : assignedNets) {
                DetailGateAndDaqEda::GateNode n;
                n.instName = an;
                n.cellType = "PO";
                n.isPrimaryOut = true;
                n.pins.push_back({ "A", an, false });
                allNodes.push_back(std::move(n));
            }
        }
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Berkeley Boolean Equation Netlist (.eqn%ls — %zu inputs, %zu outputs, %zu gates)",
               modelName.empty() ? L"" : (L" " + PathUtils::Utf8ToWide(modelName)).c_str(),
               addedPi.size(), addedPo.size(), eqnCount);

    return DetailGateAndDaqEda::RenderGateDagToBoard(titleBuf, allNodes, outBoard);
}

} // namespace Formats
