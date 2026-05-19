#pragma once

#include <GoApi/Params/Params.h>

namespace Go {
namespace Params {

/**
 * A parameter group containing an enabled flag and a value of type T.
 * Additional (likely optional) fields can be added to this type in the future.
 */
template<typename T>
struct ExtendedValue : ParamSet
{
public:
    Go::Params::Value<bool> enabled;
    Go::Params::Value<T> value;

    ExtendedValue() : ParamSet("ExtendedValue")
    {
        Register("enabled", enabled, "Enabled");
        Register("value", value, "Value");
    }

    // When enabled, the value field will automatically become readOnly whilst enabled is false.
    void EnableValueReadOnlyToggle()
    {
        if (!valueReadOnlyToggleEnabled)
        {
            value.EnableReadOnly().readOnly = !enabled.Get();
            enabled.OnUpdatedEvent().AddListener(std::bind(&ExtendedValue::UpdateValueReadOnly, this));

            valueReadOnlyToggleEnabled = true;
        }
    }

    // This will update the value's readOnly property based on whether enabled is true or not.
    // Will do nothing if EnableValueReadOnlyToggle has not been called.
    void UpdateValueReadOnly()
    {
        if (valueReadOnlyToggleEnabled)
        {
            value.readOnly = !enabled.Get();
        }
    }

private:
    bool valueReadOnlyToggleEnabled = false;
};

}}
