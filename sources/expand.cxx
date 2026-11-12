#include "headers/expand.hxx"

std::string expand(const std::string &text, const Lookup &lookup) {
    std::string out;
    for (size_t p = 0; p < text.size();) {
        if (text[p] != '{') { out += text[p++]; continue; }
        size_t q = text.find('}', p);
        if (q == std::string::npos) { out.append(text, p, std::string::npos); break; }
        if (auto value = lookup(text.substr(p + 1, q - p - 1))) {
            out += *value;
            p = q + 1;
        } else {
            out += '{';   // not ours: keep the brace and scan on, inner {tokens} may still match
            ++p;
        }
    }
    return out;
}
