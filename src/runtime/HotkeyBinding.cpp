#include "runtime/HotkeyBinding.h"

#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace stui::runtime {
namespace {

constexpr std::array kHotkeyOptions{
    HotkeyOption{"None", std::nullopt},
    HotkeyOption{"F1", 0x3B}, HotkeyOption{"F2", 0x3C},
    HotkeyOption{"F3", 0x3D}, HotkeyOption{"F4", 0x3E},
    HotkeyOption{"F5", 0x3F}, HotkeyOption{"F6", 0x40},
    HotkeyOption{"F7", 0x41}, HotkeyOption{"F8", 0x42},
    HotkeyOption{"F9", 0x43}, HotkeyOption{"F10", 0x44},
    HotkeyOption{"F11", 0x57}, HotkeyOption{"F12", 0x58},
    HotkeyOption{"A", 0x1E}, HotkeyOption{"B", 0x30},
    HotkeyOption{"C", 0x2E}, HotkeyOption{"D", 0x20},
    HotkeyOption{"E", 0x12}, HotkeyOption{"F", 0x21},
    HotkeyOption{"G", 0x22}, HotkeyOption{"H", 0x23},
    HotkeyOption{"I", 0x17}, HotkeyOption{"J", 0x24},
    HotkeyOption{"K", 0x25}, HotkeyOption{"L", 0x26},
    HotkeyOption{"M", 0x32}, HotkeyOption{"N", 0x31},
    HotkeyOption{"O", 0x18}, HotkeyOption{"P", 0x19},
    HotkeyOption{"Q", 0x10}, HotkeyOption{"R", 0x13},
    HotkeyOption{"S", 0x1F}, HotkeyOption{"T", 0x14},
    HotkeyOption{"U", 0x16}, HotkeyOption{"V", 0x2F},
    HotkeyOption{"W", 0x11}, HotkeyOption{"X", 0x2D},
    HotkeyOption{"Y", 0x15}, HotkeyOption{"Z", 0x2C},
    HotkeyOption{"0", 0x0B}, HotkeyOption{"1", 0x02},
    HotkeyOption{"2", 0x03}, HotkeyOption{"3", 0x04},
    HotkeyOption{"4", 0x05}, HotkeyOption{"5", 0x06},
    HotkeyOption{"6", 0x07}, HotkeyOption{"7", 0x08},
    HotkeyOption{"8", 0x09}, HotkeyOption{"9", 0x0A},
    HotkeyOption{"INSERT", 0xD2}, HotkeyOption{"DELETE", 0xD3},
    HotkeyOption{"HOME", 0xC7}, HotkeyOption{"END", 0xCF},
    HotkeyOption{"PAGEUP", 0xC9}, HotkeyOption{"PAGEDOWN", 0xD1},
    HotkeyOption{"UP", 0xC8}, HotkeyOption{"DOWN", 0xD0},
    HotkeyOption{"LEFT", 0xCB}, HotkeyOption{"RIGHT", 0xCD},
    HotkeyOption{"TILDE", 0x29}, HotkeyOption{"BACKSLASH", 0x2B},
    HotkeyOption{"NUMPAD0", 0x52}, HotkeyOption{"NUMPAD1", 0x4F},
    HotkeyOption{"NUMPAD2", 0x50}, HotkeyOption{"NUMPAD3", 0x51},
    HotkeyOption{"NUMPAD4", 0x4B}, HotkeyOption{"NUMPAD5", 0x4C},
    HotkeyOption{"NUMPAD6", 0x4D}, HotkeyOption{"NUMPAD7", 0x47},
    HotkeyOption{"NUMPAD8", 0x48}, HotkeyOption{"NUMPAD9", 0x49},
};

}  // namespace

std::span<const HotkeyOption> hotkeyOptions() noexcept {
    return kHotkeyOptions;
}

HotkeyBinding::HotkeyBinding(std::filesystem::path configPath)
    : HotkeyBinding(std::make_shared<PluginConfigFile>(std::move(configPath))) {}

HotkeyBinding::HotkeyBinding(std::shared_ptr<PluginConfigFile> config)
    : m_config(std::move(config)) {}

bool HotkeyBinding::load() {
    const std::scoped_lock lock(mutex_);
    if (!m_config) {
        return false;
    }
    const auto config = m_config->read();
    if (!config) {
        return false;
    }
    if (!config->contains("hotkey")) {
        key_.reset();
        return save();
    }
    const auto& hotkey = config->at("hotkey");
    if (hotkey.is_null()) {
        key_.reset();
        return true;
    }
    if (hotkey.is_number_unsigned()) {
        key_ = hotkey.get<std::uint32_t>();
        return true;
    }
    if (hotkey.is_number_integer()) {
        const auto value = hotkey.get<std::int64_t>();
        if (value >= 0 && value <= UINT32_MAX) {
            key_ = static_cast<std::uint32_t>(value);
            return true;
        }
        return false;
    }
    if (hotkey.is_string()) {
        auto name = hotkey.get<std::string>();
        for (auto& character : name) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        for (const auto& option : kHotkeyOptions) {
            if (option.key && option.label == name) {
                key_ = option.key;
                return true;
            }
        }
    }
    return false;
}

bool HotkeyBinding::select(std::optional<std::uint32_t> key) {
    const std::scoped_lock lock(mutex_);
    const auto previous = key_;
    key_ = key;
    if (save()) {
        return true;
    }
    key_ = previous;
    return false;
}

bool HotkeyBinding::matches(std::uint32_t key) const noexcept {
    const std::scoped_lock lock(mutex_);
    return key_ && *key_ == key;
}

std::optional<std::uint32_t> HotkeyBinding::key() const noexcept {
    const std::scoped_lock lock(mutex_);
    return key_;
}

std::string HotkeyBinding::label() const {
    const std::scoped_lock lock(mutex_);
    if (!key_) {
        return "None";
    }
    for (const auto& option : kHotkeyOptions) {
        if (option.key == key_) {
            return std::string(option.label);
        }
    }
    std::ostringstream label;
    label << "DIK 0x" << std::uppercase << std::hex << *key_;
    return label.str();
}

bool HotkeyBinding::save() const {
    if (!m_config) {
        return false;
    }
    return m_config->update([key = key_](nlohmann::json& config) -> ConfigUpdateResult {
        config["hotkey"] = key ? nlohmann::json(*key) : nlohmann::json(nullptr);
        return {};
    }).has_value();
}

}  // namespace stui::runtime
