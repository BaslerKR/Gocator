#ifndef GOAPI_KEYBOARD_SHORTCUT_H
#define GOAPI_KEYBOARD_SHORTCUT_H

#include "GoApi/Params/Params.h"

namespace Go {
namespace Params {

struct KeyboardShortcut : Go::Params::AnchorBase
{
    static constexpr const kChar* MODIFIER_SHIFT    = "shift";
    static constexpr const kChar* MODIFIER_CTRL     = "ctrl";
    static constexpr const kChar* MODIFIER_ALT      = "alt";
    static constexpr const kChar* MODIFIER_META     = "meta";  // This is the Windows "Window" key or the Mac's Command or "clover leaf" key.

    // GoPxL should follow https://developer.mozilla.org/en-US/docs/Web/API/UI_Events/Keyboard_event_key_values
    // for keyboard values of keys and modifiers.
    //
    // A list of one or more keyboard keys. This field should be used
    // for keys like:
    //  - 0 to 9
    //  - a to z
    //  - !, @, #, etc.
    // This field is not for keys like "ctrl", "alt", "fn" etc which
    // are considered modifier keys which should go in the "modifiers" field.
    Go::Params::Value<std::vector<std::string>> keyList;
    // This field is for keys like "ctrl", "alt", "shift", "meta" (Windows key or Mac Command key).
    // See implementation plan for details:
    //    https://docs.google.com/document/d/1gYuQzioSb3qI4IC4seayyJWprMyXBUbCoR6LTH6i8dg
    Go::Params::Value<std::vector<std::string>> modifiers;

    KeyboardShortcut() : AnchorBase("KeyboardShortcut")
    {
        Register("keyList", keyList);
        Register("modifiers", modifiers);

        // Keyboard key information is set by BE and read-only by client.
        keyList.EnableReadOnly().readOnly = true;
        modifiers.EnableReadOnly().readOnly = true;

        // Define a new anchor type for keyboard shortcuts and call it "keyAction".
        anchorType = "keyAction";

        // Front end code does not use the schema information, so they are removed to
        // reduce the amount of information sent between the back end and front end.
        this->DeregisterSchema("properties");
        this->DeregisterSchema("sortOrder");
        this->DeregisterSchema("title");
    }

    // This function sets the fields of a keyboard shortcut entry.
    void Set(const std::initializer_list<std::string>& keyList, const std::initializer_list<std::string>& modifiers, const std::string& url)
    {
        this->keyList = keyList;
        this->modifiers = modifiers;
        this->url = url;
    }
};

}}

#endif  // GOAPI_KEYBOARD_SHORTCUT_H
