
#include "includes.hpp"
#include "ui/record_layer.hpp"
#include "ui/game_ui.hpp"
#include "ui/clickbot_layer.hpp"
#include "ui/macro_editor.hpp"
#include "ui/render_settings_layer.hpp"
#include "hacks/layout_mode.hpp"
#include "hacks/show_trajectory.hpp"

#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCTouchDispatcher.hpp>

class $modify(CCKeyboardDispatcher) {
  bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timestamp) {

    auto& g = Global::get();

    int keyInt = static_cast<int>(key);
    if (g.allKeybinds.contains(keyInt) && !isKeyRepeat) {
      for (size_t i = 0; i < 6; i++) {
        if (std::find(g.keybinds[i].begin(), g.keybinds[i].end(), keyInt) != g.keybinds[i].end())
          g.heldButtons[i] = isKeyDown;
      }
    }

    // if (key == enumKeyCodes::KEY_L && !isKeyRepeat && isKeyDown) {
    // }

    // if (key == enumKeyCodes::KEY_F && !isKeyRepeat && isKeyDown && PlayLayer::get()) {
    //   log::debug("POS DEBUG {}", PlayLayer::get()->m_player1->getPosition());
    //   log::debug("POS2 DEBUG {}", PlayLayer::get()->m_player2->getPosition());
    // }

    // if (key == enumKeyCodes::KEY_J && !isKeyRepeat && isKeyDown && PlayLayer::get()) {
    //   std::string str = ZipUtils::decompressString(PlayLayer::get()->m_level->m_levelString.c_str(), true, 0);
    //   log::debug("{}", str);
    // }

    return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timestamp);
  }
};

#ifdef GEODE_IS_WINDOWS

void onKeybind(bool down, std::string_view id) {
  auto& g = Global::get();

  if (!down || (LevelEditorLayer::get() && !g.mod->getSettingValue<bool>("editor_keybinds")) || g.mod->getSettingValue<bool>("disable_keybinds"))
    return;

  if (g.state != state::recording && g.mod->getSettingValue<bool>("recording_only_keybinds"))
    return;

  if (id == "open_menu") {
    if (g.layer) {
      static_cast<RecordLayer*>(g.layer)->onClose(nullptr);
      return;
    }

    RecordLayer::openMenu();
  }

  if (id == "toggle_recording")
    Macro::toggleRecording();

  if (id == "toggle_playing")
    Macro::togglePlaying();

  if (id == "toggle_frame_stepper" && PlayLayer::get())
    Global::toggleFrameStepper();

  if (id == "step_frame")
    Global::frameStep();

  if (id == "toggle_speedhack")
    Global::toggleSpeedhack();

  if (id == "show_trajectory") {
    g.mod->setSavedValue("macro_show_trajectory", !g.mod->getSavedValue<bool>("macro_show_trajectory"));

    if (g.layer) {
      if (static_cast<RecordLayer*>(g.layer)->trajectoryToggle)
        static_cast<RecordLayer*>(g.layer)->trajectoryToggle->toggle(g.mod->getSavedValue<bool>("macro_show_trajectory"));
    }

    g.showTrajectory = g.mod->getSavedValue<bool>("macro_show_trajectory");
    if (!g.showTrajectory) ShowTrajectory::trajectoryOff();
  }

  if (id == "toggle_render" && PlayLayer::get()) {
    bool result = Renderer::toggle();

    if (result && Global::get().renderer.recording)
      Notification::create("Started Rendering", NotificationIcon::Info)->show();

    if (g.layer) {
      if (static_cast<RecordLayer*>(g.layer)->renderToggle)
        static_cast<RecordLayer*>(g.layer)->renderToggle->toggle(Global::get().renderer.recording);
    }

  }

  if (id == "toggle_noclip") {
    g.mod->setSavedValue("macro_noclip", !g.mod->getSavedValue<bool>("macro_noclip"));

    if (g.layer) {
      if (static_cast<RecordLayer*>(g.layer)->noclipToggle)
        static_cast<RecordLayer*>(g.layer)->noclipToggle->toggle(g.mod->getSavedValue<bool>("macro_noclip"));
    }
  }
}

$execute {
  const std::vector<std::pair<std::string, bool>> keybindSettings = {
    {"open_menu", false},
    {"toggle_recording", false},
    {"toggle_playing", false},
    {"toggle_speedhack", false},
    {"toggle_frame_stepper", false},
    {"step_frame", true},
    {"toggle_render", false},
    {"toggle_noclip", false},
    {"show_trajectory", false},
  };

  for (auto const& entry : keybindSettings) {
    std::string id = entry.first;
    bool allowRepeat = entry.second;

    geode::listenForKeybindSettingPresses(id, [id, allowRepeat](geode::Keybind const&, bool down, bool repeat, double) {
      if (repeat && !allowRepeat) return;
      onKeybind(down, id);
    });
  }
}

#endif
