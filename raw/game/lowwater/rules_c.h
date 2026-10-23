/* Low Water rules (checks, skills, sheet, meters) behind a plain C ABI, so the module can be rebuilt in
 * another language later. Behaviour is fixed by the language-neutral traces (docs/story-trace.md) and
 * the content data; this header carries no C++ types. The state is one opaque handle, every call is a
 * step on it, and errors are return codes plus lw_rules_error(). Strings are UTF-8, NUL-terminated;
 * returned strings stay valid until the next call on the same handle. */
#ifndef RAW_GAME_LOWWATER_RULES_C_H
#define RAW_GAME_LOWWATER_RULES_C_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct lw_rules lw_rules;

/* Parse the records in `text` (LWD/1) and create the rules state. NULL on failure (lw_rules_last_error). */
lw_rules* lw_rules_create(const char* lwd_text, unsigned int seed);
void lw_rules_destroy(lw_rules* r);
const char* lw_rules_last_error(void);

/* Build the starting sheet from an archetype id. 0 on success, -1 when the archetype is unknown. */
int lw_rules_become(lw_rules* r, const char* archetype);
/* The effective level of a skill (base, training, worn-item mods, environment modifiers). */
double lw_rules_skill(lw_rules* r, const char* skill);
double lw_rules_attr(lw_rules* r, const char* attr);
/* A passive check: 1 when the skill total meets dc, else 0. The label of dc goes to *label (may be NULL). */
int lw_rules_passive(lw_rules* r, const char* skill, double dc, const char** label);

/* Meters. */
double lw_rules_meter(const lw_rules* r, const char* id);
int lw_rules_meter_add(lw_rules* r, const char* id, double amount);   /* 0, or -1 for an unknown meter */
/* The next full meter's scene node, or NULL when none is due. *meter receives the meter id. */
const char* lw_rules_take_full(lw_rules* r, const char** meter);

#ifdef __cplusplus
}
#endif
#endif
