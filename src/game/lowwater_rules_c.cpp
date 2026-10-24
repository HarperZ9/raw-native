// The C ABI of the rules module (raw/game/lowwater/rules_c.h): a thin shell over Rules.
#include "raw/game/lowwater/rules_c.h"
#include "raw/game/lowwater/rules.hpp"
#include <memory>

using namespace raw::game::lowwater;
namespace {
std::string g_error;
}
struct lw_rules {
    raw::story::Book book; std::unique_ptr<Rules> rules; std::string scratch, scratch2;
};
extern "C" {
lw_rules* lw_rules_create(const char* text, unsigned int seed) {
    try {
        auto r = std::make_unique<lw_rules>(); r->book = raw::story::load_book({{"rules.lwd", text ? text : ""}});
        r->rules = std::make_unique<Rules>(r->book, seed);
        if (!r->rules->ruleset().errors.empty()) { g_error = r->rules->ruleset().errors[0]; return nullptr; }
        return r.release();
    } catch (std::exception& e) { g_error = e.what(); return nullptr; }
}
void lw_rules_destroy(lw_rules* r) { delete r; }
const char* lw_rules_last_error(void) { return g_error.c_str(); }
int lw_rules_become(lw_rules* r, const char* a) { try { r->rules->become(a); return 0; } catch (std::exception& e) { g_error = e.what(); return -1; } }
double lw_rules_skill(lw_rules* r, const char* s) { return r->rules->skill(s); }
double lw_rules_attr(lw_rules* r, const char* a) { return r->rules->attr(a); }
int lw_rules_passive(lw_rules* r, const char* skill, double dc, const char** label) {
    raw::story::Json a = raw::story::Json::arr(); a.add(raw::story::Json::str("k")).add(raw::story::Json::str(skill)).add(raw::story::Json::num(dc));
    raw::story::Json out = r->rules->answer("checks.passive", a);
    if (label) { r->scratch = out.str_or("label"); *label = r->scratch.c_str(); }
    return out.get("pass")->b ? 1 : 0;
}
double lw_rules_meter(const lw_rules* r, const char* id) { return r->rules->meter(id); }
int lw_rules_meter_add(lw_rules* r, const char* id, double n) { try { r->rules->add_meter(id, n); return 0; } catch (std::exception& e) { g_error = e.what(); return -1; } }
const char* lw_rules_take_full(lw_rules* r, const char** meter) {
    raw::story::Json j = r->rules->take_full(); if (j.k != raw::story::Json::Obj) return nullptr;
    r->scratch = j.str_or("node"); r->scratch2 = j.str_or("id"); if (meter) *meter = r->scratch2.c_str(); return r->scratch.c_str();
}
}
