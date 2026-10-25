// Dominion Rod Weapon
//
// The Dominion Rod's glowing ball (daCrod_c) already has an attack capsule, but
// the game gives it an attack type that only statues react to and an attack
// power of 0, so it never hurts anything else.
//
// This mod adds a normal damage type and a real attack power to that capsule
// every frame, while leaving the original statue type in place so the rod
// still animates statues exactly as before. The ball behaves like a thrown
// weapon: it hurts what it hits, then flies back to Link.
//
// Settings are in this mod's panel in the Mods window:
//   - master on/off toggle
//   - optional "only after the rod's power is restored" toggle
//   - damage amount
// All settings are saved.

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_crod.h"
#include "d/d_cc_d.h"

#include <cstdint>
#include <cstdio>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

DEFINE_HOOK(&daCrod_c::execute, CrodExecute);

namespace {

// ---- Settings --------------------------------------------------------------

constexpr int64_t kDefaultDamage = 2;
constexpr int64_t kMinDamage = 1;
constexpr int64_t kMaxDamage = 99;

constexpr int kStepCount = 4;
constexpr int kStepDeltas[kStepCount] = {-5, -1, 1, 5};
constexpr const char* kStepLabels[kStepCount] = {"-5", "-1", "+1", "+5"};

ConfigVarHandle s_enabledVar = 0;
ConfigVarHandle s_restoredVar = 0;
ConfigVarHandle s_damageVar = 0;

bool s_enabled = true;           // master switch
bool s_requireRestored = false;  // only deal damage once the rod's tip is active
int64_t s_damage = kDefaultDamage;

UiElementHandle s_damageText = 0;
int s_steps[kStepCount];

void refresh_settings() {
    bool flag = s_enabled;
    if (svc_config->get_bool(mod_ctx, s_enabledVar, &flag) == MOD_OK) {
        s_enabled = flag;
    }
    flag = s_requireRestored;
    if (svc_config->get_bool(mod_ctx, s_restoredVar, &flag) == MOD_OK) {
        s_requireRestored = flag;
    }
    int64_t value = s_damage;
    if (svc_config->get_int(mod_ctx, s_damageVar, &value) == MOD_OK) {
        s_damage = value;
    }
}

void set_damage(int64_t value) {
    if (value < kMinDamage) {
        value = kMinDamage;
    }
    if (value > kMaxDamage) {
        value = kMaxDamage;
    }
    if (svc_config->set_int(mod_ctx, s_damageVar, value) == MOD_OK) {
        s_damage = value;
    }
}

// ---- Hook ------------------------------------------------------------------

HookAction on_crod_execute_pre(ModContext*, void* args, void*, void*) {
    daCrod_c* rod = mods::arg<daCrod_c*>(args, 0);
    if (rod == nullptr) {
        return HOOK_CONTINUE;
    }

    // Param 6 is a helper object used for the iron ball; it has no attack
    // capsule set up, so leave it alone.
    if (fopAcM_GetParam(rod) == 6) {
        return HOOK_CONTINUE;
    }

    bool active = s_enabled;
    if (active && s_requireRestored) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        active = link != nullptr && link->checkCopyRodTopUse();
    }

    // Set (or restore) the capsule's attack info before the game submits it
    // for collision this frame. The statue type is always kept.
    if (active) {
        rod->mAtCps.SetAtType(AT_TYPE_1000000 | AT_TYPE_NORMAL_SWORD);
        rod->mAtCps.SetAtAtp(static_cast<int>(s_damage));
    } else {
        rod->mAtCps.SetAtType(AT_TYPE_1000000);
        rod->mAtCps.SetAtAtp(0);
    }
    return HOOK_CONTINUE;
}

// ---- Mods panel ------------------------------------------------------------

void on_step_pressed(ModContext*, void* userData) {
    const int* delta = static_cast<const int*>(userData);
    set_damage(s_damage + *delta);
}

void on_reset_pressed(ModContext*, void*) {
    set_damage(kDefaultDamage);
}

void update_damage_text() {
    if (s_damageText == 0) {
        return;
    }
    char text[48];
    std::snprintf(text, sizeof(text), "Current: %lld", static_cast<long long>(s_damage));
    svc_ui->elem_set_text(mod_ctx, s_damageText, text);
}

ModResult build_panel(ModContext*, UiElementHandle pane, void*, ModError*) {
    UiControlDesc enabled = UI_CONTROL_DESC_INIT;
    enabled.kind = UI_CONTROL_TOGGLE;
    enabled.label = "Dominion Rod deals damage";
    enabled.help_rml = "Turn this off to put the rod back to normal.";
    enabled.binding = UI_BINDING_CONFIG_VAR;
    enabled.config_var = s_enabledVar;
    ModResult result = svc_ui->pane_add_control(mod_ctx, pane, &enabled, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc restored = UI_CONTROL_DESC_INIT;
    restored.kind = UI_CONTROL_TOGGLE;
    restored.label = "Only after the rod's power is restored";
    restored.help_rml =
        "Uses the game's own check for whether the rod's tip is active. "
        "Leave this off to have damage always available.";
    restored.binding = UI_BINDING_CONFIG_VAR;
    restored.config_var = s_restoredVar;
    result = svc_ui->pane_add_control(mod_ctx, pane, &restored, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    svc_ui->pane_add_section(mod_ctx, pane, "Damage");
    s_damageText = 0;
    svc_ui->pane_add_text(mod_ctx, pane, "", &s_damageText);

    UiRowDesc rowDesc = UI_ROW_DESC_INIT;
    rowDesc.align = UI_ROW_ALIGN_CENTER;
    UiElementHandle row = 0;
    result = svc_ui->pane_add_row(mod_ctx, pane, &rowDesc, &row);
    if (result != MOD_OK) {
        return result;
    }

    for (int i = 0; i < kStepCount; i++) {
        s_steps[i] = kStepDeltas[i];
        UiControlDesc button = UI_CONTROL_DESC_INIT;
        button.kind = UI_CONTROL_BUTTON;
        button.label = kStepLabels[i];
        button.on_pressed = on_step_pressed;
        button.user_data = &s_steps[i];
        svc_ui->pane_add_control(mod_ctx, row, &button, nullptr);
    }

    UiControlDesc reset = UI_CONTROL_DESC_INIT;
    reset.kind = UI_CONTROL_BUTTON;
    reset.label = "Reset damage";
    reset.on_pressed = on_reset_pressed;
    svc_ui->pane_add_control(mod_ctx, pane, &reset, nullptr);

    update_damage_text();
    return MOD_OK;
}

ModResult update_panel(ModContext*, void*, ModError*) {
    update_damage_text();
    return MOD_OK;
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ConfigVarDesc enabledDesc = CONFIG_VAR_DESC_INIT;
    enabledDesc.name = "damageEnabled";
    enabledDesc.type = CONFIG_VAR_BOOL;
    enabledDesc.default_bool = true;
    ModResult result = svc_config->register_var(mod_ctx, &enabledDesc, &s_enabledVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register the damageEnabled setting");
    }

    ConfigVarDesc restoredDesc = CONFIG_VAR_DESC_INIT;
    restoredDesc.name = "requireRestored";
    restoredDesc.type = CONFIG_VAR_BOOL;
    restoredDesc.default_bool = false;
    result = svc_config->register_var(mod_ctx, &restoredDesc, &s_restoredVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register the requireRestored setting");
    }

    ConfigVarDesc damageDesc = CONFIG_VAR_DESC_INIT;
    damageDesc.name = "damage";
    damageDesc.type = CONFIG_VAR_INT;
    damageDesc.default_int = kDefaultDamage;
    result = svc_config->register_var(mod_ctx, &damageDesc, &s_damageVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register the damage setting");
    }

    refresh_settings();

    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    panel.update = update_panel;
    result = svc_ui->register_mods_panel(mod_ctx, &panel);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register the mod panel");
    }

    mods::hook::add_pre<CrodExecute>(on_crod_execute_pre);
    svc_log->info(mod_ctx, "Dominion Rod Weapon loaded");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError* error) {
    refresh_settings();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError* error) {
    return MOD_OK;
}

}
