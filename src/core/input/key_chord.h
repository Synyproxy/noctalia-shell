#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct KeyChord {
  std::uint32_t sym = 0;       // XKB keysym
  std::uint32_t modifiers = 0; // KeyMod bitmask

  bool operator==(const KeyChord&) const = default;
};

// Throws std::runtime_error if spec contains a Super-family modifier.
// Bare printable keys (e.g. "1", "a") are accepted — UI-level policy is
// enforced by KeybindRecorder's ModifierPolicy, not here.
[[nodiscard]] std::optional<KeyChord> parseKeyChordSpec(std::string_view spec);
[[nodiscard]] std::string keyChordToString(const KeyChord& chord);
[[nodiscard]] std::string keyChordDisplayLabel(const KeyChord& chord);
[[nodiscard]] bool keyChordMatches(const KeyChord& chord, std::uint32_t sym, std::uint32_t modifiers) noexcept;
[[nodiscard]] bool isPrintableKey(std::uint32_t sym);

// True for a text-producing key a focused text input should own: a printable
// codepoint (>= U+0020, excluding DEL), no Ctrl/Alt/Super held, not a dead-key
// preview. Such a key is text even when its codepoint doubles as a keybind
// chord (e.g. Space is bound to Validate but must type a space when focused).
[[nodiscard]] bool isPlainPrintableKey(std::uint32_t utf32, std::uint32_t modifiers, bool preedit) noexcept;

// True for the keys a focused text input needs for editing besides printable text:
// caret movement (arrows, Home/End, Page Up/Down), Backspace/Delete, Return/KP_Enter
// and Tab, with at most Shift held. Ctrl/Alt/Super chords are left to the caller, so a
// panel can still capture e.g. ctrl+return while an input is focused.
[[nodiscard]] bool isTextEditingKey(std::uint32_t sym, std::uint32_t modifiers) noexcept;
