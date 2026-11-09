// The rules module through its C ABI (raw/game/lowwater/rules_c.h). The expected values are worked from
// the JS rules (rules/checks.mjs, sheet.mjs, world/pressure.mjs): a skill is its attribute plus training
// plus worn-item mods; a check passes at total >= dc; a meter clamps to [min, max] and queues its scene at max.
#include "check.hpp"
#include "raw/game/lowwater/rules_c.h"
#include <cstring>

int main() {
    const char* lwd =
        "@rules\ncheck-mode: fixed\nattr-min: 1\n\n@attr pen\n\n@skill sum\nattr: pen\n\n@item hat\nslot: hat\nmod: skill.sum +1\n\n"
        "@archetype clerk\nattr: pen 3\nskill: sum 2\nitem: hat 1\n\n"
        "@meter cold\nmax: 10\nstart: 2\nfull: chilled\n\n@node chilled\nnar: Cold.\n";
    lw_rules* r = lw_rules_create(lwd, 7); CHECK(r != nullptr);
    CHECK(lw_rules_skill(r, "sum") == 1);                 // attr-min 1, no training
    CHECK(lw_rules_become(r, "clerk") == 0);
    CHECK(lw_rules_attr(r, "pen") == 3); CHECK(lw_rules_skill(r, "sum") == 6);   // 3 + 2 training + 1 hat
    const char* label = nullptr;
    CHECK(lw_rules_passive(r, "sum", 6, &label) == 1); CHECK(std::strcmp(label, "Trivial") == 0);
    CHECK(lw_rules_passive(r, "sum", 8, &label) == 0); CHECK(std::strcmp(label, "Easy") == 0);
    CHECK(lw_rules_become(r, "nobody") == -1);
    CHECK(lw_rules_meter(r, "cold") == 2);
    CHECK(lw_rules_meter_add(r, "cold", 99) == 0); CHECK(lw_rules_meter(r, "cold") == 10);   // clamped
    const char* id = nullptr; const char* node = lw_rules_take_full(r, &id);
    CHECK(node && std::strcmp(node, "chilled") == 0 && std::strcmp(id, "cold") == 0);
    CHECK(lw_rules_take_full(r, &id) == nullptr);          // taken once
    CHECK(lw_rules_meter_add(r, "nope", 1) == -1);
    lw_rules_destroy(r);
    return raw_test_summary();
}
