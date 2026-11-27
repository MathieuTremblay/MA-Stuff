#pragma once
// Touch widgets shared by the UI files (ui.cpp, ui_cues.cpp, ui_launcher.cpp).
#include <string>

#include "imgui.h"
#include "state.hpp"

struct App;

namespace ui {

inline constexpr ImVec4 kAccent{1.00f, 0.47f, 0.10f, 1.f};
inline constexpr ImVec4 kFlashCol{1.00f, 0.85f, 0.12f, 1.f};  // FLASH scenes
inline constexpr ImVec4 kAccentDim{0.55f, 0.25f, 0.05f, 1.f};
inline constexpr ImVec4 kDanger{0.85f, 0.12f, 0.15f, 1.f};
inline constexpr ImVec4 kGo{0.15f, 0.75f, 0.35f, 1.f};
inline constexpr ImVec4 kBack{0.20f, 0.55f, 1.00f, 1.f};  // background layer
inline constexpr ImVec4 kText{0.92f, 0.92f, 0.94f, 1.f};
inline constexpr ImVec4 kTextDim{0.55f, 0.55f, 0.60f, 1.f};

float S();  // current UI scale
ImU32 col32(const Color3& c, float a = 1.f);
bool Toggle(const char* label, bool on, ImVec2 size, ImVec4 onColor = kAccent);
bool Choice(const char* id, const char* const* labels, int n, int* value, int cols = 0, float h = 0);
bool OnOff(const char* label, bool* v, float h = 0);
void SectionLabel(const char* text);
void Hint(const char* text);
bool TouchSlider(const char* label, float* v, float lo, float hi, const char* valueText);
// def != null: double tap resets to *def, dragging snaps at it, a tick marks it
bool TouchSliderDef(const char* label, float* v, float lo, float hi, const char* valueText, const float* def);
bool Pct(const char* label, float* v);
void TouchScroll();
void SwatchLabel(ImVec2 p, ImVec4 col, const char* text);

// On-screen keyboard (touch PCs often have no keyboard). One shared keyboard, drawn by KeyboardFrame()
// at the end of the UI frame. OpenKeyboard() asks for text; KeyboardResult(id) returns true once, with
// the text, when the user presses OK for that request.
void OpenKeyboard(const char* id, const std::string& initial, const char* title, bool numeric = false);
bool KeyboardResult(const char* id, std::string& out);
bool KeyboardIsOpen();
void KeyboardFrame(App& a);
void QuitConfirmFrame(App& a);

// Panels from the other UI files
void CuesPanel(App& a);
void Launcher(App& a);
bool ShowModeLocked(App& a);              // true while show mode blocks editing
void ShowModeButton(App& a, ImVec2 size);

}  // namespace ui
