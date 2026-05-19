#ifndef GOAPI_PARAMS_H
#define GOAPI_PARAMS_H

#include <kFireSync/Data/kColor.h>
#include <kVision/L3d/kL3dTransform3d.h>
#include <GoApi/Params/UnsignedValueMin.h>
#include <GoApi/Properties/Nodes.h>

namespace Go {
namespace Params {

#define GOAPI_PARAMS_UNNAMED        "<unnamed>"
/* II8N: Parameter validation. */
#define GOPARAMS_VALIDATION_SOURCE  bepgettext("beApp", "Params validation")

#define GOAPI_PARAMS_DEGREE_UNIT                             "deg"      ///< To be consistent with tool side
#define GOAPI_PARAMS_MILLIMETER_UNIT                         "mm"
#define GOAPI_PARAMS_MINUTE_UNIT                             "min"

// Sort order value to ensure it's in position after all the BoxRegion parameters.
#define TRANSFORM_MATRIX_SORT_ORDER_NUMBER 9

    /**
 * A list of valid formats that a string can be represented as,
 * these supported types are pulled from -
 * https://json-schema.org/understanding-json-schema/reference/string.html#format
 * These values represent the format a string can take and doesn't apply if the value
 * is not a string value.
 */
#define GOAPI_PARAMS_FORMAT_EMPTY                           ""
#define GOAPI_PARAMS_FORMAT_DATE_TIME                       "date-time"
#define GOAPI_PARAMS_FORMAT_DATE                            "date"
#define GOAPI_PARAMS_FORMAT_TIME                            "time"
#define GOAPI_PARAMS_FORMAT_DURATION                        "duration"
#define GOAPI_PARAMS_FORMAT_EMAIL                           "email"
#define GOAPI_PARAMS_FORMAT_IDN_EMAIL                       "idn-email"
#define GOAPI_PARAMS_FORMAT_HOSTNAME                        "hostname"
#define GOAPI_PARAMS_FORMAT_IDN_HOSTNAME                    "idn-hostname"
#define GOAPI_PARAMS_FORMAT_IPV4                            "ipv4"
#define GOAPI_PARAMS_FORMAT_IPV6                            "ipv6"
#define GOAPI_PARAMS_FORMAT_UUID                            "uuid"
#define GOAPI_PARAMS_FORMAT_URI                             "uri"
#define GOAPI_PARAMS_FORMAT_URI_REF                         "uri-reference"
#define GOAPI_PARAMS_FORMAT_IRI                             "iri"
#define GOAPI_PARAMS_FORMAT_IRI_REF                         "iri-reference"
#define GOAPI_PARAMS_FORMAT_URI_TEMPLATE                    "uri-template"
#define GOAPI_PARAMS_FORMAT_JSON_POINTER                    "json-pointer"
#define GOAPI_PARAMS_FORMAT_RELATIVE_JSON_POINTER           "relative-json-pointer"
#define GOAPI_PARAMS_FORMAT_REGEX                           "regex"
#define GOAPI_PARAMS_FORMAT_CODE                            "code"
#define GOAPI_PARAMS_FORMAT_CODE_PYTHON                     "code-python"

// GOS-3627: Possible scales for a GoParams Value.
enum struct ScaleOption : k32u
{
    SCALE_NONE = 1,
    SCALE_LINEAR = 2,
    SCALE_LOGARITHMIC = 3
};

/**
 * Connectedness of points in point set region.
 */
enum struct PointSetRegionMode : k32u
{
    MODE_POINTS = 1,             ///< Individual points
    MODE_OPEN   = 2,             ///< Open, connected in order
    MODE_CLOSED = 3,              ///< Closed loop
    MODE_DEFAULT = k32U_MAX
};

/**
 * Graphic marker shape enumeration.
 */
enum struct MarkerShape : k32s
{
    SHAPE_DEFAULT = -1,
    SHAPE_CROSS = 0,        ///<  Cross marker shape.
    SHAPE_PLUS = 1,         ///<  Plus marker shape.
    SHAPE_CIRCLE = 2,       ///<  Circle marker shape.
    SHAPE_SQUARE = 3,       ///<  Square marker shape.
    SHAPE_TRIANGLE = 4,     ///<  Triangle marker shape.
    SHAPE_DIAMOND = 5       ///<  Diamond marker shape.
};

// GOS-12478 : Status Codes.
enum struct StatusCode : k32u
{
    NONE = 0,
    STATUS_OK = 1,
    STATUS_NOK = 2,

    STATUS_GREEN = 11,
    STATUS_YELLOW = 12,
    STATUS_ORANGE = 13,
    STATUS_RED = 14,

    STATUS_INFO = 21,
    STATUS_WARN = 22,
    STATUS_ERROR = 23,
    STATUS_CRITICAL = 24
};

/**
 * A bitmask that determines the visibility of params based on which URL params are selected.
 * Default/base case is a visibility mask of '0' - param should be shown.
 *
 * If a param has visibility types set, it will only be shown if ALL of the visibility types it has
 * are included in urlParams.
 *      e.g. param A = 000110 = {ADVANCED, EXPERIMENTAL}, urlParams = {ADVANCED, EXPERIMENTAL}  = shown
 *           param B = 000110 = {ADVANCED, EXPERIMENTAL}, urlParams = {ADVANCED}                = hidden
 *           param C = 000010 = {ADVANCED},               urlParams = {ADVANCED, EXPERIMENTAL}  = shown
 *
 * See GOS-912 for more details.
 */
enum struct VisibilityType : k32u
{
    HIDDEN       = 1 << 0,    // Never show this param.
    ADVANCED     = 1 << 1,    // Used by a small segment of users. Less visible.
    EXPERIMENTAL = 1 << 2,    // Trial mode, not supported, may be removed.
    DEBUG        = 1 << 3,    // Debug or developer use.
    DEPRECATED   = 1 << 4,    // Not intended to be used any more, but still functional.
    INTERNAL     = 1 << 5     // For internal use.
};

inline const char* Visibility_Name(VisibilityType v)
{
    switch (v)
    {
    case VisibilityType::HIDDEN:        return "HIDDEN";
    case VisibilityType::ADVANCED:      return "ADVANCED";
    case VisibilityType::EXPERIMENTAL:  return "EXPERIMENTAL";
    case VisibilityType::DEBUG:         return "DEBUG";
    case VisibilityType::DEPRECATED:    return "DEPRECATED";
    case VisibilityType::INTERNAL:      return "INTERNAL";

    default:      return "[Unknown Visibility]";
    }
}

/**
* A base parameter class for sharing common properties to be inherited by other classes.
*/
struct ParamsBase
{
    Go::Properties::Value<std::string> title;
    Go::Properties::Value<bool> readOnly = false;
    Go::Properties::Value<bool> used = true;


    // GOS-9641: replaced raw pointers with smart pointers to ensure memory is released automatically.
    // Pointer to a meta string. Only initialized once meta is enabled.
    std::unique_ptr<Go::Properties::Value<std::string>> meta;
    // Pointer to a visibility bitmask. Only initialized once visibility is enabled.
    std::unique_ptr<Go::Properties::Value<k32u>> visibility;
    // GOS-9641: add description as top level property for all parameter based derived
    // classes to inherit. Only initialized once meta is enabled.
    std::unique_ptr<Go::Properties::Value<std::string>> description;

    Go::Properties::Value<k32u> sortOrder;


    // GOS-5307: Ensure readOnly change is only rejected if validation was checked beforehand.
    // This should allow job loading to change readOnly values, as they don't run validation.
    mutable bool readOnlyValidateFailed = false;

    ParamsBase(){}

    virtual ~ParamsBase()
    {
    };

    /**
     * Validates ParamsBase parameters.
     *
     * @param loggingEnabled    Shows log messages if true, nothing otherwise. Default to true.
     */
    void CheckValidation(const bool loggingEnabled = true) const
    {
        if (readOnlyEnabled && readOnly.Get())
        {
            readOnlyValidateFailed = true;

            if (loggingEnabled)
            {
                // GOS-4877: Change readOnly to publish a warning but not fail validation outright.
                // GOS-5061: Only publish this log to the user in debug builds.
                GoLogUserWarnIfDev(GOPARAMS_VALIDATION_SOURCE, "%s is read-only and will not fail validation.",
                    this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str());
            }
        }
        else
        {
            readOnlyValidateFailed = false;
        }
    }

    /**
     * Manages the storage for visibility information.
     * Enabling this property creates the storage while disabling it deletes the storage.
     *
     * @param enabled   Whether this property should be enabled or disabled.
     *
     * @throws       Go::Exception if the deprecated "usedEnabled" property is also enabled.
     */
    void EnableVisibility(bool enabled = true)
    {
        if (enabled)
        {
            if (visibility == nullptr)
            {
                // GOS-8091: TODO: Remove this check when used is removed.
                if (!usedEnabled)
                {
                    visibility = std::make_unique<Go::Properties::Value<k32u>>();
                    *visibility = 0;
                }
                else
                {
                    GoThrowMsg(kERROR_PARAMETER, "Used is already enabled, cannot enable visibility.");
                }
            }
        }
        else
        {
            if (visibility != nullptr)
            {
                // Free the memory.
                visibility.reset();
            }
        }
        visibilityEnabled = enabled;
    }

    /**
     * Returns true if the parameter has default visibility - visibility is not enabled, or no visibility types are set.
     *
     * @return        True if visibility is not enabled or if the bitmask is 0 (no visibility types set), false otherwise.
     */
    bool IsDefaultVisible()
    {
        return !visibilityEnabled || visibility->Get() == 0;
    }

    /**
     * Adds a specific visibility type to affect rendering of the parameter on the client.
     * @param v      The visibility type to add to the parameter.
     *
     * @throws       Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @remarks      Multiple visibility types can be added. Adding the same visibility type twice is gracefully re-accepted.
     */
    void AddVisibility(const VisibilityType v)
    {
        if (visibilityEnabled)
        {
            visibility->Modify() |= static_cast<k32u>(v);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Visibility not enabled, cannot add visibility type %s.", Visibility_Name(v));
        }
    }

    /**
     * Remove a specific visibility from a parameter.
     *
     * @param v      The visibility to remove from the parameter.
     *
     * @throws       Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void RemoveVisibility(const VisibilityType v)
    {
        if (visibilityEnabled)
        {
            visibility->Modify() &= ~static_cast<k32u>(v);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Visibility not enabled, cannot remove visibility type %s.", Visibility_Name(v));
        }
    }

    /**
     * Check whether a parameter has a specific visibility.
     *
     * @param v      The visibility of the parameter.
     *
     * @throws       Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return       True if the parameter has visibility type v, false otherwise.
     */
    bool HasVisibility(const VisibilityType v) const
    {
        if (visibilityEnabled)
        {
            return visibility->Get() & static_cast<k32u>(v);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Visibility not enabled, cannot check for visibility type %s.", Visibility_Name(v));
        }
    }

    /**
     * Clear all visibilities from a parameter.
     * Once all visibilities are cleared, the parameter will be visible on the front end by default.
     *
     * @throws       Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void ClearVisibility()
    {
        if (visibilityEnabled)
        {
            visibility->Modify() = 0;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Visibility not enabled, cannot clear visibility types");
        }
    }

    /**
     * Updates visibility to add/remove a visibility type if a condition is met or not.
     *
     * @param cond   The condition to determine whether visibility is added.
     * @param v      The visibility of the parameter.
     *
     * @throws       Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @remarks
     * When using HIDDEN visibility type, use negative logic if necessary.
     * See examples below:
     * @code {.cpp}
     *     // Show foo if enabled, else hide it.
     *     fooParam.UpdateVisibilityIf(!fooEnabled, VisibilityType::HIDDEN);
     * @endcode
     */
    void UpdateVisibilityIf(bool cond, const VisibilityType v)
    {
        if (visibilityEnabled)
        {
            if (cond)
            {
                visibility->Modify() |= static_cast<k32u>(v);
            }
            else
            {
                visibility->Modify() &= ~static_cast<k32u>(v);
            }
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Visibility not enabled, cannot add visibility type %s.", Visibility_Name(v));
        }
    }

    /**
     * Manages the storage for meta information.
     * Enabling this property creates the storage while disabling it deletes the storage.
     *
     * @param enabled   Whether this property should be enabled or disabled.
     */
    void EnableMeta(bool enabled = true)
    {
        if (enabled)
        {
            if (meta == nullptr)
            {
                meta = std::make_unique<Go::Properties::Value<std::string>>();
                *meta = "";
            }
        }
        else
        {
            if (meta != nullptr)
            {
                // Free the memory.
                meta.reset();
            }
        }
        metaEnabled = enabled;
    }

    /**
     * Allows user to set the 'meta' property value.
     *
     * @param   meta     The new 'meta' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void SetMeta(const std::string& meta)
    {
        if ((this->meta != nullptr) && this->MetaEnabled())
        {
            this->meta->Set(meta);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'meta' before setting the value.");
        }
    }

    /**
     * @return      Gets reference to 'meta' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetMeta() const
    {
        if (metaEnabled)
        {
            return *meta;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'meta' before using it.");
        }
    }

    // GOS-9641: added description to the params base class so this property can be inherited
    // by groups, values and arrays.
    /**
     * Manages the storage for description information.
     * Enabling this property creates the storage while disabling it deletes the storage.
     *
     * @param enabled   Whether this property should be enabled or disabled.
     */
    void EnableDescription(bool enabled = true)
    {
        if (enabled)
        {
            if (description == nullptr)
            {
                description = std::make_unique<Go::Properties::Value<std::string>>();
            }
        }
        else
        {
            if (description != nullptr)
            {
                description.reset();
            }
        }
        descriptionEnabled = enabled;
    }

    /**
     * Sets the 'description' property value.
     *
     * @param   description     The new 'description' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void SetDescription(const std::string& description)
    {
        if ((this->description != nullptr) && this->DescriptionEnabled())
        {
            this->description->Set(description);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'description' before setting the value.");
        }
    }

    /**
     * Gets the 'description' property value.
     *
     * @return      Gets reference to 'description' property when it has been enabled.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetDescription() const
    {
        if (this->descriptionEnabled)
        {
            return *description;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'description' before using it.");
        }
    }

    // GOS-9641: refactored the code to handle visibility, meta and description to first
    // manage the memory and then set the property, if property is enabled in "other".
    void CopyValues(const ParamsBase& other)
    {
        title.Set(other.title);
        sortOrder.Set(other.sortOrder);
        readOnly.Set(other.readOnly);
        used.Set(other.used);

        EnableVisibility(other.visibilityEnabled);
        if (other.visibilityEnabled)
        {
            visibility->Set(*other.visibility);
        }

        readOnlyEnabled = other.readOnlyEnabled;
        usedEnabled = other.usedEnabled;

        EnableMeta(other.metaEnabled);
        if (other.metaEnabled)
        {
            meta->Set(*other.meta);
        }

        EnableDescription(other.descriptionEnabled);
        if (other.descriptionEnabled)
        {
            description->Set(*other.description);
        }
    }

   /**
    * @return       True if visibility is enabled; false otherwise.
    */
    const bool VisibilityEnabled() const
    {
        return visibilityEnabled;
    }

   /**
    * @return       True if 'used' is enabled; false otherwise.
    */
    const bool UsedEnabled() const
    {
        return usedEnabled;
    }

    /**
     * @return       True if 'meta' is enabled; false otherwise.
     */
    const bool MetaEnabled() const
    {
        return metaEnabled;
    }

    /**
     * Gets the current status of whether the description property has been enabled or not.
     *
     * @return       True if 'description' is enabled; false otherwise.
     */
    const bool DescriptionEnabled() const
    {
        return descriptionEnabled;
    }

protected:
    bool readOnlyEnabled = false;
    bool usedEnabled = false;
    bool visibilityEnabled = false;
    bool metaEnabled = false;
    bool descriptionEnabled = false;
};

/**
 * GoProps values with extra onChange callback functionality to be used for resource update validation.
 * Currently unused. To be finished and utilized in GOS-3163.
 */
template <typename T>
struct ValidationValue : Go::Properties::Value<T>, Event<>
{
    ValidationValue() :
        Go::Properties::Value<T>()
    {
    }

    ValidationValue(const T& initialValue) :
        Go::Properties::Value<T>(initialValue)
    {
    }

    ValidationValue& Set(const T& v)
    {
        Go::Properties::Value<T>::Set(v);
        Notify();
        return *this;
    }

    ValidationValue& operator=(const T& v)
    {
        return Set(v);
    }
};

/**
 * A simple "value" parameter class, e.g. a number or a string.
 */
template <typename T>
struct Value : Go::Properties::Value<T>, ParamsBase
{
public:
    // GOS-5307: Save validated value to ensure read call
    mutable T validatedValue;

    Value()
    {
        RegisterCommonAttrs();

        T minVal = this->Get();
        bool setMin = UnsignedValueMin<T>::SetUnsignedMin(minVal);
        if (setMin)
        {
            this->EnableMinimum().SetMinimum(minVal);
        }
    }

    Value(const T& val) :
        Go::Properties::Value<T>(val)
    {
        RegisterCommonAttrs();

        T minVal = this->Get();
        bool setMin = UnsignedValueMin<T>::SetUnsignedMin(minVal);
        if (setMin)
        {
            this->EnableMinimum().SetMinimum(minVal);
        }
    }

    ~Value() override
    {
        // GOS-8269 - Deallocates any already dynamically-allocated properties.
        if (units)
        {
            delete units;
        }

        if (actual)
        {
            delete actual;
        }

        if (minimum)
        {
            delete minimum;
        }

        if (maximum)
        {
            delete maximum;
        }

        if (exclusiveMinimum)
        {
            delete exclusiveMinimum;
        }

        if (exclusiveMaximum)
        {
            delete exclusiveMaximum;
        }

        if (scale)
        {
            delete scale;
        }

        if (extendedType)
        {
            delete extendedType;
        }

        if (format)
        {
            delete format;
        }

        // The ParamsBase class destructor will be called automatically here
        // to destroy its dynamically allocated objects.
    }

    // Make this class non-copyable.
    // All the derived classes don't support copying so it makes the most sense
    // to not support it here as well.
    // Note that copy constructors/assignment operators are implicitly implemented
    // by the compiler once the destructor is implemented.
    // In this case it will implement a default copy constructor doing shallow copies of it's members
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;

    // Implementation of virtual method from IValueIo
    void ReadValue(Go::Properties::IValueReader& reader, Event<>& endReadEvent) override
    {
        // GOS-4877: Changed readOnly value doesn't fail validation anymore,
        // but still don't want to change the value.
        // GOS-5307: readOnly shouldn't prevent changes unless validation was previously run.
        if (readOnlyValidateFailed)
        {
            readOnlyValidateFailed = false;
            // If new value matches the one from validation, assume this is the same session.
            // This isn't air-tight, but helps prevent theoretical case where validation sets flag then later fails,
            // then later a new value is read without validation (such as job load).
            T newValue;
            Go::Properties::ValueIoDispatch<T>::Read(reader, newValue);
            if (Go::Properties::ValueEqFunc<T>::Eq(newValue, validatedValue))
            {
                // Reject change -- readOnly failed validation.
                return;
            }
        }

        Go::Properties::Value<T>::ReadValue(reader, endReadEvent);
    }

    Value<T>& operator=(const T& other)
    {
        this->Set(other);
        return *this;
    }

    // Node class overrides.
    void CheckValidationAndCollect(std::vector<std::string>& invalidParamsList, size_t invalidParamsMax = 0) override
    {
        try
        {
            // GOS-11202: loggingEnabled = false since we are collecting all validation errors for later display.
            this->CheckValidation(this->Get(), false);
        }
        catch (Go::Exception& e)
        {
            if (e.Status() == kERROR_PARAMETER)
            {
                invalidParamsList.push_back(e.what());
            }
            else
            {
                GoLogError("Non-kERROR_PARAMETER exception '%s' caught during validation.", e.what());
            }
        }
    }

    void RetrieveChangedParameters(std::vector <Go::Properties::Node*>& changedParams) override
    {
        if (this->hasChanged)
        {
            changedParams.push_back(this);
        }
    }

    void ResetChangedParameters() override
    {
        this->hasChanged = false;
    }

    // GOS-9641: fixed a bug where only the values for visibiity/meta were copied but not the schema,
    // by always calling the EnableXXX() function which registers/deregisters the property schema.
    void CopyValues(const Go::Properties::Node& other) override
    {
        // Skip copying this parameter if neverCopy is true.
        if (neverCopy)
        {
            return;
        }

        Go::Properties::Value<T>::CopyValues(other);

        auto& otherValue = dynamic_cast<const Value<T>&>(other);
        ParamsBase::CopyValues(otherValue);

        if (readOnlyEnabled)
        {
            EnableReadOnly();
        }

        if (usedEnabled)
        {
            EnableUsed();
        }

        // Call these to register these properties into the schema.
        this->EnableVisibility(otherValue.VisibilityEnabled());
        this->EnableMeta(otherValue.MetaEnabled());
        this->EnableDescription(otherValue.DescriptionEnabled());

        this->EnableUnits(otherValue.UnitsEnabled());
        this->EnableActual(otherValue.ActualEnabled());
        this->EnableMinimum(otherValue.MinimumEnabled());
        this->EnableMaximum(otherValue.MaximumEnabled());
        this->EnableExclusiveMinimum(otherValue.ExclusiveMinimumEnabled());
        this->EnableExclusiveMaximum(otherValue.ExclusiveMaximumEnabled());
        this->EnableScale(otherValue.ScaleEnabled());
        this->EnableFormat(otherValue.FormatEnabled());

        if (this->UnitsEnabled())
        {
            this->units->Set(otherValue.GetUnits().Get());
        }

        if (this->ActualEnabled())
        {
            this->actual->Set(otherValue.GetActual().Get());
        }

        if (this->MinimumEnabled())
        {
            this->minimum->Set(otherValue.GetMinimum().Get());
        }

        if (this->MaximumEnabled())
        {
            this->maximum->Set(otherValue.GetMaximum().Get());
        }

        if (this->ExclusiveMinimumEnabled())
        {
            this->exclusiveMinimum->Set(otherValue.GetExclusiveMinimum().Get());
        }

        if (this->ExclusiveMaximumEnabled())
        {
            this->exclusiveMaximum->Set(otherValue.GetExclusiveMaximum().Get());
        }

        if (this->ScaleEnabled())
        {
            this->scale->Set(otherValue.GetScale().Get());
        }

        // GOS-8269 - Manually set the 'extendedType' because there is no API to enable 'extendedType'
        // without specifying the actual string used.
        if (otherValue.ExtendedTypeEnabled())
        {
            this->SetExtendedType(otherValue.GetExtendedType().Get());
        }
        else
        {
            this->SetExtendedType("");
        }

        if (this->FormatEnabled())
        {
            this->format->Set(otherValue.GetFormat().Get());
        }
    }

    void CheckValidation(const T& newValue, const bool loggingEnabled = true) const override
    {
        // Check validation at the ParamsBase level before proceeding.
        ParamsBase::CheckValidation(loggingEnabled);
        if (readOnlyValidateFailed)
        {
            validatedValue = newValue;
        }

        Go::Properties::Value<T>::CheckValidation(newValue);

        if (minimumEnabled && newValue < minimum->Get())
        {
            /* II8N: Parameter validation. */
            const kChar* errorMsgFormat = bepgettext("beApp", "%1$s is set to be less than its minimum.");
            const kChar* errorParamTitle = this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str();

            if (loggingEnabled)
            {
                GoLogUserError(GOPARAMS_VALIDATION_SOURCE, errorMsgFormat, errorParamTitle);
            }

            GoThrowMsg(kERROR_PARAMETER, errorMsgFormat, errorParamTitle);
        }

        if (maximumEnabled && newValue > maximum->Get())
        {
            /* II8N: Parameter validation. */
            const kChar* errorMsgFormat = bepgettext("beApp", "%1$s is set to be greater than its maximum.");
            const kChar* errorParamTitle = this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str();

            if (loggingEnabled)
            {
                GoLogUserError(GOPARAMS_VALIDATION_SOURCE, errorMsgFormat, errorParamTitle);
            }

            GoThrowMsg(kERROR_PARAMETER, errorMsgFormat, errorParamTitle);
        }

        if (exclusiveMinimumEnabled && newValue <= exclusiveMinimum->Get())
        {
            /* II8N: Parameter validation. */
            const kChar* errorMsgFormat = bepgettext("beApp", "%1$s is set to be less than or equal to its exclusive minimum.");
            const kChar* errorParamTitle = this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str();

            if (loggingEnabled)
            {
                GoLogUserError(GOPARAMS_VALIDATION_SOURCE, errorMsgFormat, errorParamTitle);
            }

            GoThrowMsg(kERROR_PARAMETER, errorMsgFormat, errorParamTitle);
        }

        if (exclusiveMaximumEnabled && newValue >= exclusiveMaximum->Get())
        {
            /* II8N: Parameter validation. */
            const kChar* errorMsgFormat = bepgettext("beApp", "%1$s is set to be greater than or equal to its exclusive maximum.");
            const kChar* errorParamTitle = this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str();

            if (loggingEnabled)
            {
                GoLogUserError(GOPARAMS_VALIDATION_SOURCE, errorMsgFormat, errorParamTitle);
            }

            GoThrowMsg(kERROR_PARAMETER, errorMsgFormat, errorParamTitle);
        }
    }

    operator const T&() const override
    {
        return Get();
    }

    /**
     * @return      Gets 'actual' value when 'actual' property is enabled, otherwise gets user entered value.
     */
    const T& Get() const override
    {
        return actualEnabled ? actual->Get() : GetUserValue();
    }

    /**
     * @return      Gets user entered value.
     */
    const T& GetUserValue() const
    {
        return Go::Properties::Value<T>::Get();
    }

    /**
     * @return      Gets reference to 'units' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetUnits() const
    {
        if (unitsEnabled)
        {
            return *units;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'units' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'actual' value when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<T>& GetActual() const
    {
        if (actualEnabled)
        {
            return *actual;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'actual' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'minimum' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<T>& GetMinimum() const
    {
        if (minimumEnabled)
        {
            return *minimum;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'minimum' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'maximum' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<T>& GetMaximum() const
    {
        if (maximumEnabled)
        {
            return *maximum;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'maximum' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'exclusiveMinimum' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<T>& GetExclusiveMinimum() const
    {
        if (exclusiveMinimumEnabled)
        {
            return *exclusiveMinimum;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'exclusiveMinimum' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'exclusiveMaximum' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<T>& GetExclusiveMaximum() const
    {
        if (exclusiveMaximumEnabled)
        {
            return *exclusiveMaximum;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'exclusiveMaximum' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'scale' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<ScaleOption>& GetScale() const
    {
        if (scaleEnabled)
        {
            return *scale;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'scale' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'extendedType' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetExtendedType() const
    {
        if (extendedTypeEnabled)
        {
            return *extendedType;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'extendedType' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'format' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetFormat() const
    {
        if (formatEnabled)
        {
            return *format;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'format' before using it.");
        }
    }

    Value<T>& EnableReadOnly()
    {
        this->RegisterSchema("readOnly", readOnly, false);
        readOnlyEnabled = true;
        return *this;
    }

    Value<T>& EnableUsed(bool enabled = true)
    {
        if (enabled)
        {
            if (!visibilityEnabled)
            {
                this->RegisterSchema("used", used, false);
                usedEnabled = true;
            }
            else
            {
                GoThrowMsg(kERROR_PARAMETER, "Visibility is already enabled, cannot enable used.");
            }
        }
        else
        {
            usedEnabled = false;
            this->DeregisterSchema("used");
        }
        return *this;
    }

    // GOS-912: visibility is newly added so we added the ability to both enable and disable.
    // GOS-7801: TODO: Other properties should gain enable/disable functionality in the future.
    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    // Multiple definitions are done instead of a template definition to allow chaining and allow for easier debugging.
    Value<T>& EnableVisibility(bool enabled = true)
    {
        ParamsBase::EnableVisibility(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("visibility", *visibility, false);
        }
        else
        {
            this->DeregisterSchema("visibility");
        }

        return *this;
    }

    /**
     * Allows user to enable or disable the 'units' property.
     *
     * @param   enabled     Whether the 'units' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableUnits(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'units' property, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->units)
            {
                units = new Go::Properties::Value<std::string>();
                this->RegisterSchema("units", *units, false);
            }
        }
        else
        {
            // To disable the 'units' property, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->UnitsEnabled())
            {
                this->DeregisterSchema("units");
            }

            // Only deallocate if object is truly allocated.
            if (this->units)
            {
                delete units;
                units = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->unitsEnabled = enabled;

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    /**
     * Allows user to enable or disable the 'description' property.
     *
     * @param   enabled     Whether the 'description' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableDescription(bool enabled = true)
    {
        ParamsBase::EnableDescription(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("description", *description, false);
        }
        else
        {
            this->DeregisterSchema("description");
        }

        return *this;
    }

    /**
     * Allows user to enable or disable the 'actual' value.
     *
     * @param   enabled     Whether the 'actual' value should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableActual(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'actual' value, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->actual)
            {
                actual = new Go::Properties::Value<T>();
                this->RegisterSchema("actual", *actual, false);
            }
        }
        else
        {
            // To disable the 'actual' value, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->ActualEnabled())
            {
                this->DeregisterSchema("actual");
            }

            // Only deallocate if object is truly allocated.
            if (this->actual)
            {
                delete actual;
                actual = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->actualEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'minimum' property.
     *
     * @param   enabled     Whether the 'minimum' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableMinimum(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'minimum' property, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->minimum)
            {
                minimum = new Go::Properties::Value<T>();
                this->RegisterSchema("minimum", *minimum, false);
            }
        }
        else
        {
            // To disable the 'minimum' property, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->MinimumEnabled())
            {
                this->DeregisterSchema("minimum");
            }

            // Only deallocate if object is truly allocated.
            if (this->minimum)
            {
                delete minimum;
                minimum = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->minimumEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'maximum' property.
     *
     * @param   enabled     Whether the 'maximum' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableMaximum(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'maximum' property, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->maximum)
            {
                maximum = new Go::Properties::Value<T>();
                this->RegisterSchema("maximum", *maximum, false);
            }
        }
        else
        {
            // To disable the 'maximum' property, if this parameter has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->MaximumEnabled())
            {
                this->DeregisterSchema("maximum");
            }

            // Only deallocate if object is truly allocated.
            if (this->maximum)
            {
                delete maximum;
                maximum = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->maximumEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'exclusiveMinimum' property.
     *
     * @param   enabled     Whether the 'exclusiveMinimum' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableExclusiveMinimum(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'exclusiveMinimum' property, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->exclusiveMinimum)
            {
                exclusiveMinimum = new Go::Properties::Value<T>();
                this->RegisterSchema("exclusiveMinimum", *exclusiveMinimum, false);
            }
        }
        else
        {
            // To disable the 'exclusiveMinimum' property, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->ExclusiveMinimumEnabled())
            {
                this->DeregisterSchema("exclusiveMinimum");
            }

            // Only deallocate if object is truly allocated.
            if (this->exclusiveMinimum)
            {
                delete exclusiveMinimum;
                exclusiveMinimum = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->exclusiveMinimumEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'exclusiveMaximum' property.
     *
     * @param   enabled     Whether the 'exclusiveMaximum' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableExclusiveMaximum(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'exclusiveMaximum' property, if this property hasn't been allocated,
            // allocates the property and register it to the schema;
            // otherwise, do nothing.
            if (!this->exclusiveMaximum)
            {
                exclusiveMaximum = new Go::Properties::Value<T>();
                this->RegisterSchema("exclusiveMaximum", *exclusiveMaximum, false);
            }
        }
        else
        {
            // To disable the 'exclusiveMaximum' property, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->ExclusiveMaximumEnabled())
            {
                this->DeregisterSchema("exclusiveMaximum");
            }

            // Only deallocate if object is truly allocated.
            if (this->exclusiveMaximum)
            {
                delete exclusiveMaximum;
                exclusiveMaximum = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->exclusiveMaximumEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable both 'minimum' and 'maximum' properties.
     *
     * @param   enabled     Whether both 'minimum' and 'maximum' properties should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableMinMax(bool enabled = true)
    {
        return EnableMinimum(enabled).EnableMaximum(enabled);
    }

    /**
     * Allows user to enable or disable the 'scale' property.
     *
     * @param   enabled     Whether the 'scale' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableScale(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'scale' property, if this property hasn't been allocated,
            // allocates the property, sets the default value and register it to the schema;
            // otherwise, do nothing.
            if (!this->scale)
            {
                scale = new Go::Properties::Value<ScaleOption>(ScaleOption::SCALE_NONE);
                this->RegisterSchema("scale", *scale, false);
            }
        }
        else
        {
            // To disable the 'scale' property, if this parameter has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->ScaleEnabled())
            {
                this->DeregisterSchema("scale");
            }

            // Only deallocate if object is truly allocated.
            if (this->scale)
            {
                delete scale;
                scale = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        this->scaleEnabled = enabled;

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    Value<T>& EnableMeta(bool enabled = true)
    {
        ParamsBase::EnableMeta(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("x-meta", *meta, false);
        }
        else
        {
            this->DeregisterSchema("x-meta");
        }

        return *this;
    }

    /**
     * Allows user to enable or disable the 'format' property.
     *
     * @param   enabled     Whether the 'format' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& EnableFormat(bool enabled = true)
    {
        if (enabled)
        {
            // To enable the 'format' property, if this property hasn't been allocated,
            // allocates the property, sets the default value and register it to the schema;
            // otherwise, do nothing.
            if (!this->format)
            {
                format = new Go::Properties::Value<std::string>(GOAPI_PARAMS_FORMAT_EMPTY);
                this->RegisterSchema("format", *format, false);
            }
        }
        else
        {
            // To disable the 'format' parameter, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->FormatEnabled())
            {
                this->DeregisterSchema("format");
            }

            // Only deallocate if object is truly allocated.
            if (this->format)
            {
                delete format;
                format = nullptr;
            }
        }

        // The new property enabled state should always be updated.
        formatEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to set the 'units' property value.
     *
     * @param   units   The new 'units' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetUnits(const std::string& units)
    {
        if (this->units && this->UnitsEnabled())
        {
            this->units->Set(units);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'units' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'description' property value.
     *
     * @param   description     The new 'description' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetDescription(const std::string& description)
    {
        ParamsBase::SetDescription(description);

        return *this;
    }

    /**
     * Allows user to set the 'actual' value.
     *
     * @param   actual     The new 'actual' value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetActual(const T& actual)
    {
        if (this->actual && this->ActualEnabled())
        {
            this->actual->Set(actual);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'actual' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'minimum' property value.
     *
     * @param   minimum     The new 'minimum' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetMinimum(const T& minimum)
    {
        if (this->minimum && this->MinimumEnabled())
        {
            this->minimum->Set(minimum);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'minimum' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'maximum' property value.
     *
     * @param   maximum     The new 'maximum' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetMaximum(const T& maximum)
    {
        if (this->maximum && this->MaximumEnabled())
        {
            this->maximum->Set(maximum);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'maximum' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'exclusiveMinimum' property value.
     *
     * @param   exclusiveMinimum     The new 'exclusiveMinimum' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetExclusiveMinimum(const T& exclusiveMinimum)
    {
        if (this->exclusiveMinimum && this->ExclusiveMinimumEnabled())
        {
            this->exclusiveMinimum->Set(exclusiveMinimum);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'exclusiveMinimum' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'exclusiveMaximum' property value.
     *
     * @param   exclusiveMaximum     The new 'exclusiveMaximum' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetExclusiveMaximum(const T& exclusiveMaximum)
    {
        if (this->exclusiveMaximum && this->ExclusiveMaximumEnabled())
        {
            this->exclusiveMaximum->Set(exclusiveMaximum);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'exclusiveMaximum' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'scale' property value.
     *
     * @param   scale     The new 'scale' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetScale(const ScaleOption& scale)
    {
        if (this->scale && this->ScaleEnabled())
        {
            this->scale->Set(scale);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'scale' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'meta' property value.
     *
     * @param   meta     The new 'meta' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetMeta(const std::string& meta)
    {
        ParamsBase::SetMeta(meta);

        return *this;
    }

    /**
     * Allows user to set the new 'format' property.

     *
     * @param   extendedTypeStr     The new 'extendedTypeStr' property value.
     *
     * @return  The ref to the current object itself.
     *
     * @remarks A non-empty string will automatically enable the 'format' property,
     * @remarks An empty string will automatically disable the 'format' property.
     */
    Value<T>& SetExtendedType(const std::string& extendedTypeStr = "")
    {
        if (extendedTypeStr != "")
        {
            if (!this->extendedType)
            {
                extendedType = new Go::Properties::Value<std::string>();
                this->RegisterSchema("extendedType", *extendedType);
            }

            extendedType->Set(extendedTypeStr);

            extendedTypeEnabled = true;
        }
        else
        {
            // To disable the 'format' property by setting an empty string, if this property has been allocated already,
            // we need to deregister it from schema, and deallocate the property object;
            // otherwise, do nothing.
            if (this->ExtendedTypeEnabled())
            {
                this->DeregisterSchema("extendedType");
            }

            // Only deallocate if object is truly allocated.
            if (this->extendedType)
            {
                delete extendedType;
                extendedType = nullptr;
            }

            extendedTypeEnabled = false;
        }

        return *this;
    }

    // GOS-8134 - Can only set the format to supported formats by the standard
    // and any custom code type formats.
    // See above for a list of supported formats
    /**
     * Allows user to set the new 'format' property value. User has to enable 'format' property first.
     *
     * @param   format     The new 'format' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled, or format is invalid.
     *
     * @return  The ref to the current object itself.
     */
    Value<T>& SetFormat(const std::string& format)
    {
        if (this->format && this->FormatEnabled())
        {
            // Api should restrict users from setting unsupported values
            if (format == GOAPI_PARAMS_FORMAT_EMPTY ||
                format == GOAPI_PARAMS_FORMAT_DATE_TIME ||
                format == GOAPI_PARAMS_FORMAT_DATE ||
                format == GOAPI_PARAMS_FORMAT_TIME ||
                format == GOAPI_PARAMS_FORMAT_DURATION ||
                format == GOAPI_PARAMS_FORMAT_EMAIL ||
                format == GOAPI_PARAMS_FORMAT_IDN_EMAIL ||
                format == GOAPI_PARAMS_FORMAT_HOSTNAME ||
                format == GOAPI_PARAMS_FORMAT_IDN_HOSTNAME ||
                format == GOAPI_PARAMS_FORMAT_IPV4 ||
                format == GOAPI_PARAMS_FORMAT_IPV6 ||
                format == GOAPI_PARAMS_FORMAT_UUID ||
                format == GOAPI_PARAMS_FORMAT_URI ||
                format == GOAPI_PARAMS_FORMAT_URI_REF ||
                format == GOAPI_PARAMS_FORMAT_IRI ||
                format == GOAPI_PARAMS_FORMAT_IRI_REF ||
                format == GOAPI_PARAMS_FORMAT_URI_TEMPLATE ||
                format == GOAPI_PARAMS_FORMAT_JSON_POINTER ||
                format == GOAPI_PARAMS_FORMAT_RELATIVE_JSON_POINTER ||
                format == GOAPI_PARAMS_FORMAT_REGEX ||
                format == GOAPI_PARAMS_FORMAT_CODE ||
                format == GOAPI_PARAMS_FORMAT_CODE_PYTHON)
            {
                this->format->Set(format);
            }
            else
            {
                GoThrowMsg(kERROR_PARAMETER, "Format must be one of the supported formats.");
            }
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'Format' before setting the value.");
        }

        return *this;
    }

    /**
     * @return       Whether 'units' property has been enabled.
     */
    const bool UnitsEnabled() const
    {
        return this->unitsEnabled;
    }

    /**
     * @return       Whether 'actual' property has been enabled.
     */
    const bool ActualEnabled() const
    {
        return this->actualEnabled;
    }

    /**
     * @return       Whether 'minimum' property has been enabled.
     */
    const bool MinimumEnabled() const
    {
        return this->minimumEnabled;
    }

    /**
     * @return       Whether 'maximum' property has been enabled.
     */
    const bool MaximumEnabled() const
    {
        return this->maximumEnabled;
    }

    /**
     * @return       Whether 'exclusiveMinimum' property has been enabled.
     */
    const bool ExclusiveMinimumEnabled() const
    {
        return this->exclusiveMinimumEnabled;
    }

    /**
     * @return       Whether 'exclusiveMaximum' property has been enabled.
     */
    const bool ExclusiveMaximumEnabled() const
    {
        return this->exclusiveMaximumEnabled;
    }

    /**
     * @return       Whether 'scale' property has been enabled.
     */
    const bool ScaleEnabled() const
    {
        return this->scaleEnabled;
    }

    /**
     * @return       Whether 'extendedType' property has been enabled.
     */
    const bool ExtendedTypeEnabled() const
    {
        return this->extendedTypeEnabled;
    }

    /**
     * @return       Whether 'format' property has been enabled.
     */
    const bool FormatEnabled() const
    {
        return this->formatEnabled;
    }

    const bool NeverCopy() const
    {
        return neverCopy;
    }

    const void SetNeverCopy(bool neverCopy)
    {
        this->neverCopy = neverCopy;
    }

// private methods
private:
    void RegisterCommonAttrs()
    {
        this->RegisterSchema("title", title);
        this->RegisterSchema("sortOrder", sortOrder);
    }

// private members
private:
    // GOS-8134 - Format is private as it's a dynamic property.
    // GOS-8269 - Move all the properties into dynamically allocated properties.
    // User should not be able to access them directly.
    // User must first check whethe each property has been enabled,
    // before using Get() to obtain the reference to each property.
    Go::Properties::Value<std::string>* units = nullptr;
    Go::Properties::Value<T>* actual = nullptr;
    Go::Properties::Value<T>* minimum = nullptr;
    Go::Properties::Value<T>* maximum = nullptr;
    // Exclusive min/max don't accept edge values.
    Go::Properties::Value<T>* exclusiveMinimum = nullptr;
    Go::Properties::Value<T>* exclusiveMaximum = nullptr;
    Go::Properties::Value<ScaleOption>* scale = nullptr;
    Go::Properties::Value<std::string>* extendedType = nullptr;
    Go::Properties::Value<std::string>* format = nullptr;

    // GOS-8134 - Add boolean for each dynamically allocated property.
    // Each of those boolean indicates whether the corresponding property
    // has been enabled and in use.
    // User has no direct access to them.
    bool unitsEnabled = false;
    bool actualEnabled = false;
    bool minimumEnabled = false;
    bool maximumEnabled = false;
    bool exclusiveMinimumEnabled = false;
    bool exclusiveMaximumEnabled = false;
    bool scaleEnabled = false;
    bool extendedTypeEnabled = false;
    bool formatEnabled = false;

    // GOS-13585: Added ability to mark some parameters to never be copied via CopyValues, notably anchor url params.
    bool neverCopy = false;
};

/**
 * Custom value definition for binary that includes additional schema properties.
 */
struct BinaryFileContents : Value<Go::Properties::ByteVector>
{
public:
    void CopyValues(const Go::Properties::Node& other) override
    {
        Value::CopyValues(other);

        auto& otherValue = dynamic_cast<const BinaryFileContents&>(other);

        this->EnableContentMediaType(otherValue.ContentMediaTypeEnabled());
        this->EnableAcceptedFileTypes(otherValue.AcceptedFileTypesEnabled());

        if (this->ContentMediaTypeEnabled())
        {
            this->SetContentMediaType(otherValue.GetContentMediaType().Get());
        }

        if (this->AcceptedFileTypesEnabled())
        {
            this->SetAcceptedFileTypes(otherValue.GetAcceptedFileTypes().Get());
        }
    }

    /**
     * Enable/disable the 'contentMediaType' schema property.
     *
     * @param enabled                   Whether to enable vs disable the property.
     *
     * @return                          Reference to this object for call chaining.
     */
    BinaryFileContents& EnableContentMediaType(bool enabled = true)
    {
        if (enabled)
        {
            if (!contentMediaTypeEnabled) // If contentMediaType is already enabled, do nothing.
            {
                contentMediaType = std::make_unique<Go::Properties::Value<std::string>>();
                *contentMediaType = "";
                this->RegisterSchema("contentMediaType", *contentMediaType, false);
            }
        }
        else
        {
            if (contentMediaTypeEnabled) // If contentMediaType is already disabled, do nothing.
            {
                this->DeregisterSchema("contentMediaType");
                contentMediaType.reset();
            }
        }
        contentMediaTypeEnabled = enabled;
        return *this;
    }

    /**
     * Enable/disable the 'x-acceptedFileTypes' schema property.
     *
     * @param enabled                   Whether to enable vs disable the property.
     *
     * @return                          Reference to this object for call chaining.
     */
    BinaryFileContents& EnableAcceptedFileTypes(bool enabled = true)
    {
        if (enabled)
        {
            if (!acceptedFileTypesEnabled) // If acceptedFileTypes is already enabled, do nothing.
            {
                acceptedFileTypes = std::make_unique<Go::Properties::Value<std::vector<std::string>>>();
                acceptedFileTypes->Modify().clear();
                // The "x-" prefix marks this as a custom schema field not covered by official json spec.
                this->RegisterSchema("x-acceptedFileTypes", *acceptedFileTypes, false);
            }
        }
        else
        {
            if (acceptedFileTypesEnabled) // If acceptedFileTypes is already disabled, do nothing.
            {
                this->DeregisterSchema("x-acceptedFileTypes");
                acceptedFileTypes.reset();
            }
        }
        acceptedFileTypesEnabled = enabled;
        return *this;
    }

    /**
     * @return      Reference to 'contentMediaType' schema property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetContentMediaType() const
    {
        if (ContentMediaTypeEnabled())
        {
            return *contentMediaType;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'contentMediaType' before using it.");
        }
    }

    /**
     * @return      Reference to 'x-acceptedFileTypes' schema property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::vector<std::string>>& GetAcceptedFileTypes() const
    {
        if (AcceptedFileTypesEnabled())
        {
            return *acceptedFileTypes;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'acceptedFileTypes' before using it.");
        }
    }

    /**
     * Allows user to set the 'contentMediaType' schema property value.
     *
     * @param   contentMediaType        The new 'contentMediaType' schema property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    BinaryFileContents& SetContentMediaType(const std::string& contentMediaType)
    {
        if (this->contentMediaType && this->ContentMediaTypeEnabled())
        {
            this->contentMediaType->Set(contentMediaType);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'contentMediaType' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'x-acceptedFileTypes' schema property value.
     *
     * @param   acceptedFileTypes     The new 'x-acceptedFileTypes' schema property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    BinaryFileContents& SetAcceptedFileTypes(const std::vector<std::string>& acceptedFileTypes)
    {
        if (this->acceptedFileTypes && this->AcceptedFileTypesEnabled())
        {
            this->acceptedFileTypes->Set(acceptedFileTypes);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'acceptedFileTypes' before setting the value.");
        }

        return *this;
    }

    /**
     * @return       Whether 'contentMediaType' schema property has been enabled.
     */
    const bool ContentMediaTypeEnabled() const
    {
        return this->contentMediaTypeEnabled;
    }

    /**
     * @return       Whether 'x-acceptedFileTypes' schema property has been enabled.
     */
    const bool AcceptedFileTypesEnabled() const
    {
        return this->acceptedFileTypesEnabled;
    }

private:
    std::unique_ptr<Go::Properties::Value<std::string>> contentMediaType = nullptr;
    std::unique_ptr<Go::Properties::Value<std::vector<std::string>>> acceptedFileTypes = nullptr;

    bool contentMediaTypeEnabled = false;
    bool acceptedFileTypesEnabled = false;
};

/**
 * An enum parameter.
 */
template<typename T>
struct Enum : Value<T>
{
    Go::Properties::Value<std::vector<T>> enumVal;
    Go::Properties::Value<std::vector<std::string>> enumText;

    Enum()
    {
        RegisterCommonAttrs();
    }

    Enum(const T& val) : Value<T>(val)
    {
        RegisterCommonAttrs();
    }

    Value<T>& operator=(const T& other)
    {
        this->Set(other);
        return *this;
    }

    void CopyValues(const Go::Properties::Node& other) override
    {
        Value<T>::CopyValues(other);

        auto& otherEnum = dynamic_cast<const Enum<T>&>(other);

        enumVal.Set(otherEnum.enumVal);
        enumText.Set(otherEnum.enumText);
    }

    void CheckValidation(const T& newValue, const bool loggingEnabled = true) const override
    {
        Value<T>::CheckValidation(newValue, loggingEnabled);

        if (!HasOption(newValue))
        {
            /* II8N: Parameter validation. */
            const kChar* errorMsgFormat = bepgettext("beApp", "%1$s is set to a value outside of the enum.");
            const kChar* errorParamTitle = this->title == "" ? GOAPI_PARAMS_UNNAMED : this->title.Get().c_str();

            if (loggingEnabled)
            {
                GoLogUserError(GOPARAMS_VALIDATION_SOURCE, errorMsgFormat, errorParamTitle);
            }

            GoThrowMsg(kERROR_PARAMETER, errorMsgFormat, errorParamTitle);
        }
    }

    void ClearOptions()
    {
        enumVal.Set(std::vector<T>());
        enumText.Set(std::vector<std::string>());
    }

    void AddOption(const T& val, const std::string& text)
    {
        auto valVec = enumVal.Get();
        valVec.push_back(val);
        enumVal.Set(valVec);

        auto textVal = enumText.Get();
        textVal.push_back(text);
        enumText.Set(textVal);
    }

    size_t OptionCount() const
    {
        return enumVal.Get().size();
    }

    const T& OptionAt(size_t index) const
    {
        return enumVal.Get().at(index);
    }

    bool HasOption(const T& option) const
    {
        // Treat all empty enums as valid, even if they might have a set value.
        if (this->OptionCount() == 0)
        {
            return true;
        }

        for (auto& val : enumVal.Get())
        {
            if (val == option)
            {
                return true;
            }
        }

        return false;
    }

    /**
     * Returns the registered title of the enumerated parameter.
     *
     * @param   index   Enum value.
     * @return          Reference to the enum text (title).
     */
    const std::string& OptionTextAt(size_t index) const
    {
        return enumText.Get().at(index);
    }

protected:
    void RegisterCommonAttrs()
    {
        this->RegisterSchema("enum", enumVal);
        this->RegisterSchema("enumText", enumText);
    }
};

/**
 * Internal base class for structure-based parameters.
 *
 * This is a base class used internally by the parameter system to implement
 * parameters with multiple values, like regions. It should not be used directly
 * by user code such as tool implementations.
 *
 * A subclass of this class should have its own "extended type", which is used
 * by the client to determine how to render the parameter. Therefore adding
 * a new subclass requires also adding support in the GUI.
 */
struct StructBase : Go::Properties::Structure, ParamsBase
{
    /**
     * The "extended type" of the parameter.
     */
    Go::Properties::Value<std::string> extendedType;

    StructBase& EnableReadOnly()
    {
        this->RegisterSchema("readOnly", readOnly, false);
        readOnlyEnabled = true;
        return *this;
    }

    StructBase& EnableUsed(bool enabled = true)
    {
        if (enabled)
        {
            if (!visibilityEnabled)
            {
                this->RegisterSchema("used", used, false);
                usedEnabled = true;
            }
            else
            {
                GoThrowMsg(kERROR_PARAMETER, "Visibility is already enabled, cannot enable used.");
            }
        }
        else
        {
            usedEnabled = false;
            this->DeregisterSchema("used");
        }
        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    StructBase& EnableVisibility(bool enabled = true)
    {
        ParamsBase::EnableVisibility(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("visibility", *visibility, false);
        }
        else
        {
            this->DeregisterSchema("visibility");
        }

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    StructBase& EnableMeta(bool enabled = true)
    {
        ParamsBase::EnableMeta(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("x-meta", *meta, false);
        }
        else
        {
            this->DeregisterSchema("x-meta");
        }

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    /**
     * Allows user to enable or disable the 'description' property.
     *
     * @param   enabled     Whether the 'description' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    StructBase& EnableDescription(bool enabled = true)
    {
        ParamsBase::EnableDescription(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("description", *description, false);
        }
        else
        {
            this->DeregisterSchema("description");
        }

        return *this;
    }

    /**
     * Allows user to set the 'description' property value.
     *
     * @param   description     The new 'description' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    StructBase& SetDescription(const std::string& description)
    {
        ParamsBase::SetDescription(description);

        return *this;
    }

    // GOS-9641: fixed a bug where only the values for visibiity/meta were copied but not the schema,
    // by always calling the EnableXXX() function which registers/deregisters the property schema.
    void CopyValues(const Go::Properties::Node& other) override
    {
        Go::Properties::Structure::CopyValues(other);

        auto& otherStruct = dynamic_cast<const StructBase&>(other);
        ParamsBase::CopyValues(otherStruct);

        if (readOnlyEnabled)
        {
            EnableReadOnly();
        }

        if (usedEnabled)
        {
            EnableUsed();
        }

        if (metaEnabled)
        {
            EnableMeta();
        }

        // Call these to register these properties into the schema.
        EnableVisibility(otherStruct.VisibilityEnabled());
        EnableMeta(otherStruct.MetaEnabled());
        EnableDescription(otherStruct.DescriptionEnabled());

        extendedType.Set(otherStruct.extendedType);
        sortOrderCounter = otherStruct.sortOrderCounter;
    }

protected:
    k32u sortOrderCounter;

    /**
     * Constructs an object.
     *
     * @param   extendedTypeStr         The extended type of the parameter.
     */
    StructBase(const std::string& extendedTypeStr = "")
    {
        // GOS-14015: Start sortOrderCounter at 100 instead of 0 to allow registering params before the first param.
        // Since sortOrderCounter and sortOrder are unsigned, subtracting 1 from 0 would underflow.
        // Starting at 100 provides headroom for registering params with negative offsets.
        sortOrderCounter = 100;

        if (extendedTypeStr != "")
        {
            this->RegisterSchema("extendedType", extendedType);
            extendedType = extendedTypeStr;
        }

        this->RegisterSchema("title", title);
        this->RegisterSchema("sortOrder", sortOrder);
    }
};

/**
 * Container for a group of parameters.
 *
 * This class defines a simple container of related parameters. In the GUI a
 * group is rendered as its own section. It is possible to nest groups, but
 * deep nesting is not recommended as it negatively impacts user experience.
 */
struct Group : StructBase
{
    /**
     * By default leave room for 100 sortOrder indexes between two Register calls.
     */
    static constexpr k32s defaultSortOrderStep = 100;

    /**
     * Constructs an object.
     */
    Group(const std::string& extendedTypeStr = "") : StructBase(extendedTypeStr)
    {
    }

    /**
     * @brief Registers a parameter.
     * 
     * The order in which a parameter is registered is saved internally with a stepped integer value
     * starting from 100 upon registration within the group. This sort order value if required, can be retrieved
     * through the schema.
     *
     * @param id             Identifies the parameter over the protocol and when persisting values in job files.
     * @param param          Reference to the parameter to register.
     * @param title          The title of the parameter.
     * @param orderingStep   Optional parameter to specify a step for the next sortOrder.
     * 
     * @code{.cpp}
     * // See example below for how to use Register() with orderingStep.
     * Register("p1", p1, "p1 title");              // p1.sortOrder = 100, sortOrderCounter = 200
     * Register("p2", p2, "p2 title");              // p2.sortOrder = 200, sortOrderCounter = 300
     * Register(p3, "p3", "p3 title", 200);         // p3.sortOrder = 300, sortOrderCounter = 500
     * // orderingStep of 200 will cause the next step to advance by 200 instead of 100.
     * Register(p4, "p4", "p4 title");              // p4.sortOrder = 500, sortOrderCounter = 600
     * // This results in:
     * // p1.sortOrder = 100
     * // p2.sortOrder = 200
     * // p3.sortOrder = 300
     * // p4.sortOrder = 500
     * @endcode
     */
    template <typename PARAM_TYPE>
    void Register(const std::string& id, PARAM_TYPE& param, const std::string& title = "", const k32s orderingStep = defaultSortOrderStep)
    {
        Go::Properties::Structure::Register(id, param);

        param.title = title != "" ? title : id;

        // Save the order of registration.
        param.sortOrder = sortOrderCounter;
        sortOrderCounter += orderingStep;
    }

    void CopyValues(const Go::Properties::Node& other) override
    {
        StructBase::CopyValues(other);
    }

    /**
     * @brief Registers a parameter before an already registered parameter.
     * 
     * @tparam PARAM_TYPE      Parameter type.
     * @param existingParam    Inserts a new parameter after this parameter.
     * @param id               Identifies the parameter over the protocol and when persisting values in job files.
     * @param param            Reference to the parameter to register.
     * @param title            The title of the parameter.
     * 
     * @code{.cpp}
     * // See example below for how to use RegisterBefore().
     * Register("p1", p1, "p1 title");              // p1.sortOrder = 100, sortOrderCounter = 200
     * Register("p2", p2, "p2 title");              // p2.sortOrder = 200, sortOrderCounter = 300
     * RegisterBefore(p1, "p1pre", "p1pre title");  // p1pre.sortOrder =  99, sortOrderCounter = 300
     * RegisterBefore(p2, "p2pre", "p2pre title");  // p2pre.sortOrder =  199, sortOrderCounter = 300
     * // This results in:
     * // p1pre.sortOrder = 99
     * // p1.sortOrder = 100
     * // p2pre.sortOrder = 199
     * // p2.sortOrder = 200
     * @endcode
     */
    template <typename PARAM_TYPE>
    void RegisterBefore(const ParamsBase& existingParam, const std::string& id, PARAM_TYPE& param, const std::string& title = "")
    {
        RegisterRelative(existingParam, -1, id, param, title);
    }

    /**
     * @brief Registers a parameter after an already registered parameter.
     * 
     * @tparam PARAM_TYPE      Parameter type.
     * @param existingParam    Inserts a new parameter before this parameter.
     * @param id               Identifies the parameter over the protocol and when persisting values in job files.
     * @param param            Reference to the parameter to register.
     * @param title            The title of the parameter.
     * 
     * @code{.cpp}
     * // See example below for how to use RegisterAfter().
     * Register("p1", p1, "p1 title");              // p1.sortOrder = 100, sortOrderCounter = 200
     * Register("p2", p2, "p2 title");              // p2.sortOrder = 200, sortOrderCounter = 300
     * RegisterAfter(p1, "p1a", "p1a title");       // p1a.sortOrder = 101, sortOrderCounter = 300
     * RegisterAfter(p2, "p2a", "p2a title");       // p2a.sortOrder = 201, sortOrderCounter = 300
     * // This results in:
     * // p1.sortOrder = 100
     * // p1a.sortOrder = 101
     * // p2.sortOrder = 200
     * // p2a.sortOrder = 201
     * @endcode
     */
    template <typename PARAM_TYPE>
    void RegisterAfter(const ParamsBase& existingParam, const std::string& id, PARAM_TYPE& param, const std::string& title = "")
    {
        RegisterRelative(existingParam, 1, id, param, title);
    }

private:
    template <typename PARAM_TYPE>
    void RegisterRelative(const ParamsBase& existingParam, k32s sortOrderDiff, const std::string& id, PARAM_TYPE& param, const std::string& title = "")
    {
        // If we attempt to register "before" 0 it doesn't actually work.
        if (existingParam.sortOrder.Get() == 0 && sortOrderDiff < 0)
        {
            GoThrowMsg(kERROR_ALREADY_EXISTS, "SortOrder minimum of %u already registered for %s. Failed to register %s.",
                existingParam.sortOrder.Get(), existingParam.title.Get().c_str(), id.c_str());
        }

        const k32u newSortOrder = existingParam.sortOrder + sortOrderDiff;

        for (auto& [name, node] : children)
        {
            const auto childParam = dynamic_cast<ParamsBase*>(&node);
            if (childParam && childParam->sortOrder == newSortOrder)
            {
                GoThrowMsg(kERROR_ALREADY_EXISTS, "SortOrder %u already registered for %s. Failed to register %s.",
                    newSortOrder, name.c_str(), id.c_str());
            }
        }

        Structure::Register(id, param);

        param.title = !title.empty() ? title : id;

        // Save the order of registration.
        param.sortOrder = newSortOrder;
    }
};

// See GOS-12639 for more info on renaming Group to Object and renaming this to Group.
struct StatusGroup : Group
{
    /**
     * Constructs an object.
     */
    StatusGroup() : Group("Group")
    {
    }

    StatusGroup(bool enableStatusText, bool enableStatusCode) : Group("Group")
    {
        EnableStatusText(enableStatusText);
        EnableStatusCode(enableStatusCode);
    }

    ~StatusGroup() override
    {
        if (statusText)
        {
            delete statusText;
        }

        if (statusCode)
        {
            delete statusCode;
        }

        if (statusDescription)
        {
            delete statusDescription;
        }
    }

    void CopyValues(const Go::Properties::Node& other) override
    {
        Go::Params::Group::CopyValues(other);

        auto& otherStruct = dynamic_cast<const StatusGroup&>(other);

        if (otherStruct.statusTextEnabled)
        {
            EnableStatusText();
            SetStatusText(otherStruct.statusText->Get());
        }

        if (otherStruct.statusCodeEnabled)
        {
            EnableStatusCode();
            SetStatusCode(otherStruct.statusCode->Get());
        }

        if (otherStruct.statusDescriptionEnabled)
        {
            EnableStatusDescription();
            SetStatusDescription(otherStruct.statusDescription->Get());
        }
    }

    /**
     * @return       Whether 'statusText' property has been enabled.
     */
    const bool StatusTextEnabled() const
    {
        return this->statusTextEnabled;
    }

    /**
     * @return       Whether 'statusCode' property has been enabled.
     */
    const bool StatusCodeEnabled() const
    {
        return this->statusCodeEnabled;
    }

    /**
     * @return       Whether 'statusDescription' property has been enabled.
     */
    const bool StatusDescriptionEnabled() const
    {
        return this->statusDescriptionEnabled;
    }

    /**
     * @return      Gets reference to 'statusText' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetStatusText() const
    {
        if (statusTextEnabled)
        {
            return *statusText;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusText' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'statusCode' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<StatusCode>& GetStatusCode() const
    {
        if (statusCodeEnabled)
        {
            return *statusCode;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusCode' before using it.");
        }
    }

    /**
     * @return      Gets reference to 'statusDescription' property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<std::string>& GetStatusDescription() const
    {
        if (statusDescriptionEnabled)
        {
            return *statusDescription;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusDescription' before using it.");
        }
    }

    /**
     * Allows user to set the 'statusText' property value.
     *
     * @param   statusText   The new 'statusText' property value to set to.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void SetStatusText(const std::string& statusText)
    {
        if (this->statusText && this->StatusTextEnabled())
        {
            this->statusText->Set(statusText);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusText' before setting the value.");
        }
    }

    /**
     * Allows user to set the 'statusCode' property value.
     *
     * @param   statusCode   The new 'statusCode' property value to set to.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void SetStatusCode(Go::Params::StatusCode statusCode)
    {
        if (this->statusCode && this->StatusCodeEnabled())
        {
            this->statusCode->Set(statusCode);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusCode' before setting the value.");
        }
    }

    /**
     * Allows user to set the 'statusDescription' property value.
     *
     * @param   statusDescription   The new 'statusDescription' property value to set to.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    void SetStatusDescription(const std::string& statusDescription)
    {
        if (this->statusDescription && this->StatusDescriptionEnabled())
        {
            this->statusDescription->Set(statusDescription);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'statusDescription' before setting the value.");
        }
    }

    /**
     * Allows user to enable or disable the 'statusText' property.
     *
     * @param   enabled     Whether the 'statusText' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Group& EnableStatusText(bool enabled = true)
    {
        if (enabled)
        {
            if (!this->statusText)
            {
                statusText = new Go::Properties::Value<std::string>();
                this->RegisterSchema("statusText", *statusText, false);
            }
        }
        else
        {
            if (this->StatusTextEnabled())
            {
                this->DeregisterSchema("statusText");
            }

            if (this->statusText)
            {
                delete statusText;
                statusText = nullptr;
            }
        }

        this->statusTextEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'statusCode' property.
     *
     * @param   enabled     Whether the 'statusCode' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Group& EnableStatusCode(bool enabled = true)
    {
        if (enabled)
        {
            if (!this->statusCode)
            {
                statusCode = new Go::Properties::Value<StatusCode>(Go::Params::StatusCode::NONE);
                this->RegisterSchema("statusCode", *statusCode, false);
            }
        }
        else
        {
            if (this->StatusCodeEnabled())
            {
                this->DeregisterSchema("statusCode");
            }

            if (this->statusCode)
            {
                delete statusCode;
                statusCode = nullptr;
            }
        }

        this->statusCodeEnabled = enabled;

        return *this;
    }

    /**
     * Allows user to enable or disable the 'statusDescription' property.
     *
     * @param   enabled     Whether the 'statusDescription' property should be enabled or not.
     *
     * @return  The ref to the current object itself.
     */
    Group& EnableStatusDescription(bool enabled = true)
    {
        if (enabled)
        {
            if (!this->statusDescription)
            {
                statusDescription = new Go::Properties::Value<std::string>();
                this->RegisterSchema("statusDescription", *statusDescription, false);
            }
        }
        else
        {
            if (this->StatusDescriptionEnabled())
            {
                this->DeregisterSchema("statusDescription");
            }

            if (this->statusDescription)
            {
                delete statusDescription;
                statusDescription = nullptr;
            }
        }

        this->statusDescriptionEnabled = enabled;

        return *this;
    }

private:
    Go::Properties::Value<std::string>* statusText = nullptr;
    Go::Properties::Value<StatusCode>* statusCode = nullptr;
    Go::Properties::Value<std::string>* statusDescription = nullptr;

    bool statusTextEnabled = false;
    bool statusCodeEnabled = false;
    bool statusDescriptionEnabled = false;
};

/**
  * An array of objects.
  */
template <typename T>
struct Array : Go::Properties::Array<T>, ParamsBase
{
    Go::Properties::Value<std::string> extendedType;

    Array()
    {
        this->RegisterSchema("title", title);
        this->RegisterSchema("sortOrder", sortOrder);
    }

    template <typename... Args>
    T& Emplace(Args&&... args)
    {
        T& newElement = Go::Properties::Array<T>::Emplace(std::forward<Args>(args)...);
        return newElement;
    }

    // GOS-9641: fixed a bug where only the values for visibiity/meta were copied but not the schema,
    // by always calling the EnableXXX() function which registers/deregisters the property schema.
    void CopyValues(const Go::Properties::Node& other) override
    {
        Go::Properties::Array<T>::CopyValues(other);

        auto& otherArray = dynamic_cast<const Array<T>&>(other);
        ParamsBase::CopyValues(otherArray);

        if (readOnlyEnabled)
        {
            EnableReadOnly();
        }

        if (usedEnabled)
        {
            EnableUsed();
        }

        // Call these to register these properties into the schema.
        EnableVisibility(otherArray.VisibilityEnabled());
        EnableMeta(otherArray.MetaEnabled());
        EnableDescription(otherArray.DescriptionEnabled());

        extendedType.Set(otherArray.extendedType);

        this->EnableMinItems(otherArray.MinItemsEnabled());
        this->EnableMaxItems(otherArray.MaxItemsEnabled());

        if (this->MinItemsEnabled())
        {
            this->SetMinItems(otherArray.GetMinItems().Get());
        }

        if (this->MaxItemsEnabled())
        {
            this->SetMaxItems(otherArray.GetMaxItems().Get());
        }
    }

    Array& EnableReadOnly()
    {
        this->RegisterSchema("readOnly", readOnly, false);
        readOnlyEnabled = true;
        return *this;
    }

    Array& EnableUsed(bool enabled=true)
    {
        if (enabled)
        {
            if (!visibilityEnabled)
            {
                this->RegisterSchema("used", used, false);
                usedEnabled = true;
            }
            else
            {
                GoThrowMsg(kERROR_PARAMETER, "Visibility is already enabled, cannot enable used.");
            }
        }
        else
        {
            usedEnabled = false;
            this->DeregisterSchema("used");
        }
        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    Array& EnableVisibility(bool enabled=true)
    {
        ParamsBase::EnableVisibility(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("visibility", *visibility, false);
        }
        else
        {
            this->DeregisterSchema("visibility");
        }

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    Array& EnableMeta(bool enabled = true)
    {
        ParamsBase::EnableMeta(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("x-meta", *meta, false);
        }
        else
        {
            this->DeregisterSchema("x-meta");
        }

        return *this;
    }

    // GOS-9641: call the base class Enable function to manage the memory, then register/deregiser
    // the property schema.
    Array& EnableDescription(bool enabled = true)
    {
        ParamsBase::EnableDescription(enabled);

        if (enabled)
        {
            // Don't need to check for disable->enable transition if the call to register
            // the schema suppresses exception if the property is already registered.
            this->RegisterSchema("description", *description, false);
        }
        else
        {
            this->DeregisterSchema("description");
        }

        return *this;
    }

    /**
     * Allows user to set the 'description' property value.
     *
     * @param   description     The new 'description' property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Array& SetDescription(const std::string& description)
    {
        ParamsBase::SetDescription(description);

        return *this;
    }

    /**
     * Enable/disable the 'minItems' schema property.
     *
     * @param enabled                   Whether to enable vs disable the property.
     *
     * @return                          Reference to this object for call chaining.
     */
    Array& EnableMinItems(bool enabled = true)
    {
        if (enabled)
        {
            if (!minItemsEnabled) // If minItems is already enabled, do nothing.
            {
                minItems = std::make_unique<Go::Properties::Value<k64u>>();
                *minItems = 0;
                this->RegisterSchema("minItems", *minItems, false);
            }
        }
        else
        {
            if (minItemsEnabled) // If minItems is already disabled, do nothing.
            {
                this->DeregisterSchema("minItems");
                minItems.reset();
            }
        }
        minItemsEnabled = enabled;
        return *this;
    }

    /**
     * Enable/disable the 'maxItems' schema property.
     *
     * @param enabled                   Whether to enable vs disable the property.
     *
     * @return                          Reference to this object for call chaining.
     */
    Array& EnableMaxItems(bool enabled = true)
    {
        if (enabled)
        {
            if (!maxItemsEnabled) // If maxItems is already enabled, do nothing.
            {
                maxItems = std::make_unique<Go::Properties::Value<k64u>>();
                *maxItems = k32U_MAX;
                this->RegisterSchema("maxItems", *maxItems, false);
            }
        }
        else
        {
            if (maxItemsEnabled) // If maxItems is already disabled, do nothing.
            {
                this->DeregisterSchema("maxItems");
                maxItems.reset();
            }
        }
        maxItemsEnabled = enabled;
        return *this;
    }

    /**
     * @return      Reference to 'minItems' schema property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<k64u>& GetMinItems() const
    {
        if (MinItemsEnabled())
        {
            return *minItems;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'minItems' before using it.");
        }
    }

    /**
     * @return      Reference to 'maxItems' schema property when it has been enabled.
     *
     * @throws      Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     */
    const Go::Properties::Value<k64u>& GetMaxItems() const
    {
        if (MaxItemsEnabled())
        {
            return *maxItems;
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'maxItems' before using it.");
        }
    }

    void SetExtendedType(const std::string& extendedTypeStr = "")
    {
        if (extendedTypeStr != "")
        {
            this->RegisterSchema("extendedType", extendedType);
            extendedType = extendedTypeStr;
        }
    }

    /**
     * Allows user to set the 'minItems' schema property value.
     *
     * @param   minItems                The new 'minItems' schema property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Array& SetMinItems(k64u minItems)
    {
        if (this->minItems && this->MinItemsEnabled())
        {
            this->minItems->Set(minItems);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'minItems' before setting the value.");
        }

        return *this;
    }

    /**
     * Allows user to set the 'maxItems' schema property value.
     *
     * @param   maxItems                The new 'maxItems' schema property value.
     *
     * @throws  Go::Exception ('kERROR_PARAMETER') if property has not been enabled.
     *
     * @return  The ref to the current object itself.
     */
    Array& SetMaxItems(k64u maxItems)
    {
        if (this->maxItems && this->MaxItemsEnabled())
        {
            this->maxItems->Set(maxItems);
        }
        else
        {
            GoThrowMsg(kERROR_PARAMETER, "Must enable 'maxItems' before setting the value.");
        }

        return *this;
    }

    /**
     * @return       Whether 'minItems' schema property has been enabled.
     */
    const bool MinItemsEnabled() const
    {
        return this->minItemsEnabled;
    }

    /**
     * @return       Whether 'maxItems' schema property has been enabled.
     */
    const bool MaxItemsEnabled() const
    {
        return this->maxItemsEnabled;
    }

private:
    std::unique_ptr<Go::Properties::Value<k64u>> minItems;
    std::unique_ptr<Go::Properties::Value<k64u>> maxItems;

    bool minItemsEnabled = false;
    bool maxItemsEnabled = false;
};

/**
 * Array structure with a more consistent schema.
 * Instead of serializing the schema of each child, a single schema from a template object is
 * serialized which represents all children.
 * The template's main structure will not be serialized, only its schema.
 * In using this class, it is important to keep children consistent with the template's schema.
 */
template <typename T>
struct ConsistentArray : Array<T>
{
public:

    /**
     * @param templ             Unique pointer to a child of type T. This object will be used as the schema template to represent all child elements.
     */
    ConsistentArray(std::unique_ptr<T> templ) :
        itemsSchemaTemplate(std::move(templ))
    {
        // Overwrite Go::Properties::Array's items schema with that of itemsSchemaTemplate.
        this->DeregisterSchema("items");
        this->RegisterSchema("items", *itemsSchemaTemplate->Schema());
    }

    void CopyValues(const Go::Properties::Node& other) override
    {
        Array<T>::CopyValues(other);

        auto& otherArray = dynamic_cast<const ConsistentArray<T>&>(other);

        this->itemsSchemaTemplate->CopyValues(otherArray.ItemsSchemaTemplate());
    }

    /**
     * Returns the items schema template for schema modification.
     * Note that the values of any registered properties are unused. Only the schema is relevant.
     *
     * @return                  Reference to the items schema template.
     */
    T& ItemsSchemaTemplate()
    {
        return *itemsSchemaTemplate;
    }

    /**
     * @return                  Const reference to the items schema template.
     */
    const T& ItemsSchemaTemplate() const
    {
        return *itemsSchemaTemplate;
    }

protected:
    // Child type instance, kept for its schema.
    std::unique_ptr<T> itemsSchemaTemplate;
};

/**
 * The top-level container for parameters.
 */
using ParamSet = Group;



struct PointSetRegionAxisValue : Go::Properties::Value<k64f>
{
    Go::Properties::Value<bool> readOnly;

    PointSetRegionAxisValue()
    {
        RegisterSchema("readOnly", readOnly);
    }

    Value<k64f>& operator=(const k64f& other)
    {
        this->Set(other);
        return *this;
    }
};

/**
 * A point in point set region.
 */
struct PointSetRegionPoint : StructBase
{
    PointSetRegionAxisValue x;
    PointSetRegionAxisValue y;
    PointSetRegionAxisValue z;

    PointSetRegionPoint()
    {
        Register("x", x);
        Register("y", y);
        Register("z", z);
    }
};

struct PointSetRegion : StructBase
{
    Go::Properties::Value<k32u> maxPointCount = k32U_MAX;
    Go::Properties::Value<k32u> minPointCount = 0;
    Go::Properties::Value<MarkerShape> pointShape = MarkerShape::SHAPE_DEFAULT;
    Go::Properties::Value<k16u> pointSize = 0;
    Go::Properties::Value<PointSetRegionMode> mode;
    Go::Properties::Value<PointSetRegionMode> pointColor = PointSetRegionMode::MODE_DEFAULT;
    Go::Properties::Value<PointSetRegionMode> lineColor = PointSetRegionMode::MODE_DEFAULT;
    Go::Properties::Value<bool> showProjection = true;
    Go::Properties::Array<PointSetRegionPoint> points;

    PointSetRegion() :
        constantZEnabled(false),
        constantZValue(0.0)
    {
        RegisterSchema("maxPointCount", maxPointCount);
        RegisterSchema("minPointCount", minPointCount);
        RegisterSchema("pointShape", pointShape);
        RegisterSchema("pointSize", pointSize);
        RegisterSchema("mode", mode);
        RegisterSchema("pointColor", pointColor);
        RegisterSchema("lineColor", lineColor);
        RegisterSchema("showProjection", showProjection);

        Register("point", points);
    }

    /**
      * Copy values from one Node to this Node.
      *
      * @param other         Node to copy values from.
      */
    void CopyValues(const Node& other) override
    {
        auto& typedOther = dynamic_cast<const PointSetRegion&>(other);

        StructBase::CopyValues(other);

        maxPointCount = typedOther.maxPointCount.Get();
        minPointCount = typedOther.minPointCount.Get();
        pointShape = typedOther.pointShape.Get();
        pointSize = typedOther.pointSize.Get();
        mode = typedOther.mode.Get();
        pointColor = typedOther.pointColor.Get();
        lineColor = typedOther.lineColor.Get();
        showProjection = typedOther.showProjection.Get();

        for (size_t i = 0; i < typedOther.points.Count(); ++i)
        {
            points.At(i).x.Set(typedOther.points.At(i).x.Get());
            points.At(i).x.readOnly.Set(typedOther.points.At(i).x.readOnly);

            points.At(i).y.Set(typedOther.points.At(i).y.Get());
            points.At(i).y.readOnly.Set(typedOther.points.At(i).y.readOnly);

            points.At(i).z.Set(typedOther.points.At(i).z.Get());
            points.At(i).z.readOnly.Set(typedOther.points.At(i).z.readOnly);
        }
    }

    /**
     * Enable and set the constant z value.
     *
     * @param zValue         z value to copy.
     */
    void EnableConstantZ(k64f zValue)
    {
        constantZEnabled = true;
        constantZValue = zValue;
        UpdateConstantZ();
    }

    /**
     * Disable the constant z value.
     */
    void DisableConstantZ()
    {
        constantZEnabled = false;
        UpdateConstantZ();
    }

    /**
     * Set the point count value.
     *
     * @param count         count value to copy.
     */
    void SetPointCount(size_t count)
    {
        points.Resize(count);
        UpdateConstantZ();
    }

protected:

    bool constantZEnabled;
    k64f constantZValue;

    /**
     * Update the constant z values.
     */
    void UpdateConstantZ()
    {
        for (size_t i = 0; i < points.Count(); ++i)
        {
            points.At(i).z.readOnly = constantZEnabled;

            if (constantZEnabled)
            {
                points.At(i).z.Set(constantZValue);
            }
        }
    }
};


// Regions extended types.
/**
 * Base box region containing just region parameters that derived classes can decide what to do with.
 * It will contain all the relevant region parameters that include :
 *    X
 *    Y
 *    Z
 *    Width
 *    Length
 *    Height
 *    X Angle
 *    Y Angle
 *    Z Angle
 *
 */
struct BaseBoxRegion : Group
{
    Go::Params::Value<k64f> x;
    Go::Params::Value<k64f> y;
    Go::Params::Value<k64f> z;
    Go::Params::Value<k64f> width;
    Go::Params::Value<k64f> length;
    Go::Params::Value<k64f> height;
    Go::Params::Value<k64f> xAngle;
    Go::Params::Value<k64f> yAngle;
    Go::Params::Value<k64f> zAngle;

    BaseBoxRegion(std::string extendedTypeStr) :
        Group(extendedTypeStr)
    {
    }
};

/**
 * A full 3D box region parameter with optional angle support.
 * All parameters are optional although the respective coordinate and region dimension
 * are paired up to minimize the number of parameters in the constructor.
 * Boolean flags passed to the constructor determines which parameters are used/registered.
 * The default extended type is "TransformedBoxRegion".
 *
 * GOS-7743 - Added support to specify whether each region params units should be included or not.
 * To minimize the number of parameters in the constructor, they are paired in the same
 * way that they are enabled.
 *
 * The "sortOrder" attributes were utilized here to ensure the region parameters appear on
 * the client side in expected order, namely:
 *    X
 *    Y
 *    Z
 *    Width  (X)
 *    Length (Y)
 *    Height (Z)
 *    X Angle
 *    Y Angle
 *    Z Angle
 */
struct BoxRegion : BaseBoxRegion
{
    BoxRegion(
        bool useXWidth = true,
        bool useYLength = true,
        bool useZHeight = true,
        bool useXAngle = false,
        bool useYAngle = false,
        bool useZAngle = false,
        bool useXWidthUnits = true,
        bool useYLengthUnits = true,
        bool useZHeightUnits = true,
        bool useXAngleUnits = true,
        bool useYAngleUnits = true,
        bool useZAngleUnits = true,
        std::string extendedTypeStr = "TransformedBoxRegion") :

        BaseBoxRegion(extendedTypeStr)
    {
        if (useXWidth)
        {
            Register("x", x, "X");
            /* II8N: For Extended Type BoxRegion. */
            Register("width", width, bepgettext("beApp", "Width (X)"));
            x.sortOrder.Set(0);
            width.sortOrder.Set(3);
            if (useXWidthUnits)
            {
                x.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
                width.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
            }
        }
        else
        {
            x.EnableVisibility();
            x.AddVisibility(Go::Params::VisibilityType::HIDDEN);
            width.EnableVisibility();
            width.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }
        if (useYLength)
        {
            Register("y", y, "Y");
            /* II8N: For Extended Type BoxRegion. */
            Register("length", length, bepgettext("beApp", "Length (Y)"));
            y.sortOrder.Set(1);
            length.sortOrder.Set(4);
            if (useYLengthUnits)
            {
                y.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
                length.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
            }
        }
        else
        {
            y.EnableVisibility();
            y.AddVisibility(Go::Params::VisibilityType::HIDDEN);
            length.EnableVisibility();
            length.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }
        if (useZHeight)
        {
            Register("z", z, "Z");
            /* II8N: For Extended Type BoxRegion. */
            Register("height", height, bepgettext("beApp", "Height (Z)"));
            z.sortOrder.Set(2);
            height.sortOrder.Set(5);
            if (useZHeightUnits)
            {
                z.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
                height.EnableUnits().SetUnits(GOAPI_PARAMS_MILLIMETER_UNIT);
            }
        }
        else
        {
            z.EnableVisibility();
            z.AddVisibility(Go::Params::VisibilityType::HIDDEN);
            height.EnableVisibility();
            height.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }

        if (useXAngle)
        {
            /* II8N: For Extended Type BoxRegion. */
            Register("xAngle", xAngle, bepgettext("beApp", "X Angle"));
            xAngle.sortOrder.Set(6);
            if (useXAngleUnits)
            {
                xAngle.EnableUnits().SetUnits(GOAPI_PARAMS_DEGREE_UNIT);
            }
        }
        else
        {
            xAngle.EnableVisibility();
            xAngle.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }
        if (useYAngle)
        {
            /* II8N: For Extended Type BoxRegion. */
            Register("yAngle", yAngle, bepgettext("beApp", "Y Angle"));
            yAngle.sortOrder.Set(7);
            if (useYAngleUnits)
            {
                yAngle.EnableUnits().SetUnits(GOAPI_PARAMS_DEGREE_UNIT);
            }
        }
        else
        {
            yAngle.EnableVisibility();
            yAngle.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }
        if (useZAngle)
        {
            /* II8N: For Extended Type BoxRegion. */
            Register("zAngle", zAngle, bepgettext("beApp", "Z Angle"));
            zAngle.sortOrder.Set(8);
            if (useZAngleUnits)
            {
                zAngle.EnableUnits().SetUnits(GOAPI_PARAMS_DEGREE_UNIT);
            }
        }
        else
        {
            zAngle.EnableVisibility();
            zAngle.AddVisibility(Go::Params::VisibilityType::HIDDEN);
        }
    }

    /**
     * Set this structure's values with values passed.
     *
     * @param xVal          x value.
     * @param yVal          y value.
     * @param zVal          z value.
     * @param widthVal      width value.
     * @param lengthVal     length value.
     * @param heightVal     height value.
     */
    void Set(k64f xVal, k64f yVal, k64f zVal, k64f widthVal, k64f lengthVal, k64f heightVal)
    {
        x = xVal;
        y = yVal;
        z = zVal;
        width = widthVal;
        length = lengthVal;
        height = heightVal;
    }

    /**
     * Set this structure's angle values with values passed.
     *
     * @param xAngleVal     x angle value.
     * @param yAngleVal     y angle value.
     * @param zAngleVal     z angle value.
     */
    void SetAngles(k64f xAngleVal, k64f yAngleVal, k64f zAngleVal)
    {
        xAngle = xAngleVal;
        yAngle = yAngleVal;
        zAngle = zAngleVal;
    }

    /*
     * Enables or disables the color attribute. This will be initialized to black when enabled.
     * This is implicitly called by SetColor if not already enabled.
     * 
     * @param enable        Whether to enable or disable the color attribute.
     */
    void EnableColor(bool enable)
    {
        if (enable && !color)
        {
            color = std::make_unique<Go::Params::Value<kColor>>();
            Register("color", *color, bepgettext("beApp", "Color"));
            color->Set(kCOLOR_BLACK);

            // GOS-13268: Color should not be editable by the user.
            // FE should know to hide this field in the UI.
            color->EnableReadOnly().readOnly = true;
        }
        else if (!enable && color)
        {
            Deregister("color");
            color.reset();
        }
    }

    /*
     * Sets the color of the box region. 
     * Calling this will register the color attribute to this group if it isn't already.
     * 
     * @param newColor      New color to set.
     */
    void SetColor(kColor newColor)
    {
        if (!color)
        {
            EnableColor(true);
        }

        // kCOLOR_WHITE is indistingishable from k32U_NULL, which is sent as json null to clients.
        // To prevent this, kCOLOR_WHITE's alpha byte will be shifted from 255 to 254.
        if (newColor == kCOLOR_WHITE)
        {
            newColor = 0xFEFFFFFF;
        }

        // Set the color.
        color->Set(newColor);
    }

    /*
     * Returns the color of the box region. Returns 0 if not registered.
     * 
     * @return              The color of the box region. Will return 0 if not registered.
     */
    kColor Color()
    {
        if (!color)
        {
            return 0;
        }

        return color->Get();
    }

    /**
     * Converts this structure's values to a kRect3d64f structure object.
     * NOTE:
     *  - kRect3d64f's height field corresponds to Gocator length field along the Y-axis.
     *  - kRect3d64f's depth field corresponds to Gocator height field along the Z-axis.
     *
     * @return  The converted structure.
     */
    kRect3d64f ToRect3d() const
    {
        kRect3d64f rect3d;

        rect3d.x = x;
        rect3d.y = y;
        rect3d.z = z;
        rect3d.width = width;
        rect3d.height = length;
        rect3d.depth = height;

        return rect3d;
    }

    /**
     * Update this structure's values from the kRect3d64f structure object passed.
     * NOTE:
     *  - kRect3d64f's height field correspnds to Gocator length field along the Y-axis.
     *  - kRect3d64f's depth field correspnds to Gocator height field along the Z-axis.
     *
     * @param rect3d         kRect3d64f structure object.
     */
    void FromRect3d(const kRect3d64f& rect3d)
    {
        x = rect3d.x;
        y = rect3d.y;
        z = rect3d.z;
        width = rect3d.width;
        length = rect3d.height;
        height = rect3d.depth;
    }

private:
    std::unique_ptr<Go::Params::Value<kColor>> color;
};

/**
 * A subset of the 3D BoxRegion only enabling parameters for a 2D profile region,
 * namely x, z, width, height and yAngle. yAngle is disabled by default.
 */
struct ProfileRegion : BoxRegion
{
    ProfileRegion(bool includeYRotation = false) :
        BoxRegion(true, false, true, false, includeYRotation, false)
    {
    }

    /**
     * Set profile region's values with values passed.
     *
     * @param xVal          x value.
     * @param zVal          z value.
     * @param widthVal      width value.
     * @param heightVal     height value.
     * @param yAngleVal     y angle value.
     */
    void Set(k64f xVal, k64f zVal, k64f widthVal, k64f heightVal, k64f yAngleVal = 0.0)
    {
        x = xVal;
        z = zVal;
        width = widthVal;
        height = heightVal;
        yAngle = yAngleVal;
    }
};

/**
 * A subset of the 3D BoxRegion only enabling parameters for a 2D surface region.
 * namely x, y, width, length and zAngle. zAngle is disabled by default.
 *
 * @param includeZRotation      Set to true if want to use Z angle, otherwise set to false.
 *                              Default to not using Z angle so give a purely 2-D region.
*/
struct SurfaceRegion2d : BoxRegion
{
    SurfaceRegion2d(bool includeZRotation = false) :
        BoxRegion(true, true, false, false, false, includeZRotation)
    {
    }

    /**
     * Set surface region's values with values passed.
     *
     * @param xVal          x value.
     * @param yVal          y value.
     * @param widthVal      width value.
     * @param lengthVal     length value.
     * @param zAngleVal     z angle value.
     */
    void Set(
        k64f xVal,
        k64f yVal,
        k64f widthVal,
        k64f lengthVal,
        k64f zAngleVal = 0.0)
    {
        x = xVal;
        y = yVal;
        width = widthVal;
        length = lengthVal;
        zAngle = zAngleVal;
    }
};

/**
 * GOS-3029: A subset of the 3D BoxRegion only enabling parameters for a 3D surface region but
 * with only Z angle (ie. no X angle or Y angle).
 * In other words, this surface region provides X/Y/Z/width/length/height/Zangle.
 * This is what VE team wants.
 *
 * @param includeZRotation      Set to true if want to use Z angle, otherwise set to false.
 *                              Default (arbitrarily) to not use Z angle.
*/
struct SurfaceRegion3d : BoxRegion
{
    SurfaceRegion3d(bool includeZRotation = false) :
        BoxRegion(true, true, true, false, false, includeZRotation)
    {}

    /**
     * Set surface region's values with values passed.
     *
     * @param xVal          x value.
     * @param yVal          y value.
     * @param zVal          z value.
     * @param widthVal      width value.
     * @param lengthVal     length value.
     * @param heightVal     height value.
     * @param zAngleVal     z angle value.
     */
    void Set(
        k64f xVal,
        k64f yVal,
        k64f zVal,
        k64f widthVal,
        k64f lengthVal,
        k64f heightVal,
        k64f zAngleVal = 0.0)
    {
        x = xVal;
        y = yVal;
        z = zVal;
        width = widthVal;
        length = lengthVal;
        height = heightVal;
        zAngle = zAngleVal;
    }
};

// 3D Transform matrix structure.
/**
 * A 3D transformation matrix parameter.
        [ xx xy xz xt ]
        [ yx yy yz yt ]
        [ zx zy zz zt ]
        [ 0  0  0  1  ]

 * This matrix is represented as 16 element array in the following order:
 * xx xy xz xt yx yy yx yt zx zy zz zt 0 0 0 1
 * An empty array will be used to indicate an invalid transform.
 */
struct Transform3d : Go::Params::Value<std::vector<k64f>>
{
    Transform3d()
    {
        this->EnableReadOnly();
        this->readOnly = true;
        this->SetExtendedType("Transform3d");
        Reset();
    }

    /**
     * Reset this transform's values to values corresponding to
     * 4 x 4 identity matrix. This by definition makes the transform valid.
     *
     *
     */
    void Reset()
    {
        std::vector<k64f> matrixVector = { 1.0, 0.0, 0.0, 0.0,
                                           0.0, 1.0, 0.0, 0.0,
                                           0.0, 0.0, 1.0, 0.0,
                                           0.0, 0.0, 0.0, 1.0 };

        Set(matrixVector);
    }

    void SetAsInvalid()
    {
        std::vector<k64f> matrixVector;
        Set(matrixVector);
    }

    bool IsValid()
    {
        return !Get().empty();
    }

    /**
     * Returns a kL3dTransform3d transform with values copied from this
     * structure's transform object. As the last row is irrelevant to
     * the kL3dTransform3d structure, it is not copied to this structure.
     *
     * @return  The kL3dTransform3d transform.
     */
    kL3dTransform3d ToTransform()
    {
        kL3dTransform3d transformOut;

        if (!Get().empty())
        {
            transformOut.xx = Get()[0];
            transformOut.xy = Get()[1];
            transformOut.xz = Get()[2];
            transformOut.xt = Get()[3];
            transformOut.yx = Get()[4];
            transformOut.yy = Get()[5];
            transformOut.yz = Get()[6];
            transformOut.yt = Get()[7];
            transformOut.zx = Get()[8];
            transformOut.zy = Get()[9];
            transformOut.zz = Get()[10];
            transformOut.zt = Get()[11];
        }
        else
        {
            // Default to identity matrix.
            transformOut.xx = 1.0;
            transformOut.xy = 0.0;
            transformOut.xz = 0.0;
            transformOut.xt = 0.0;
            transformOut.yx = 0.0;
            transformOut.yy = 1.0;
            transformOut.yz = 0.0;
            transformOut.yt = 0.0;
            transformOut.zx = 0.0;
            transformOut.zy = 0.0;
            transformOut.zz = 1.0;
            transformOut.zt = 0.0;
        }

        return transformOut;
    }

    /**
     * Set this structure's transform object values with values in
     * the passed-in transform. As the kL3dTransform3d structure
     * does not contain values for the last row of the 4x4 matrix,
     * default data is inserted here.
     *
     * @param transformIn   transform values to copy from
     */
    void FromTransform(const kL3dTransform3d& transformIn)
    {
        std::vector<k64f> matrixVector;

        matrixVector.push_back(transformIn.xx);
        matrixVector.push_back(transformIn.xy);
        matrixVector.push_back(transformIn.xz);
        matrixVector.push_back(transformIn.xt);
        matrixVector.push_back(transformIn.yx);
        matrixVector.push_back(transformIn.yy);
        matrixVector.push_back(transformIn.yz);
        matrixVector.push_back(transformIn.yt);
        matrixVector.push_back(transformIn.zx);
        matrixVector.push_back(transformIn.zy);
        matrixVector.push_back(transformIn.zz);
        matrixVector.push_back(transformIn.zt);
        matrixVector.push_back(0.0);
        matrixVector.push_back(0.0);
        matrixVector.push_back(0.0);
        matrixVector.push_back(1.0);

        Set(matrixVector);
    }

    /**
     * Checks whether the passed kL3dTransform3d structure components
     * are equal to this structure's corresponding components.
     *
     * @param transformOther   transform values to copy from
     *
     * @return  true if equivalent, false otherwise.
     */
    bool Equals(const kL3dTransform3d& transformOther)
    {
        if (Get().empty())
        {
            return false;
        }

        if (kMath_NearlyEquals64f(Get()[0], transformOther.xx) &&
            kMath_NearlyEquals64f(Get()[1], transformOther.xy) &&
            kMath_NearlyEquals64f(Get()[2], transformOther.xz) &&
            kMath_NearlyEquals64f(Get()[3], transformOther.xt) &&
            kMath_NearlyEquals64f(Get()[4], transformOther.yx) &&
            kMath_NearlyEquals64f(Get()[5], transformOther.yy) &&
            kMath_NearlyEquals64f(Get()[6], transformOther.yz) &&
            kMath_NearlyEquals64f(Get()[7], transformOther.yt) &&
            kMath_NearlyEquals64f(Get()[8], transformOther.zx) &&
            kMath_NearlyEquals64f(Get()[9], transformOther.zy) &&
            kMath_NearlyEquals64f(Get()[10], transformOther.zz) &&
            kMath_NearlyEquals64f(Get()[11], transformOther.zt))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
};


// Transformed regions derived types.

/**
 * A full 3D box region with transform and optional angle support.
 * Individual parameters can be individually configured but the default
 * is having all of them enabled.
 * Parameters are x, y, z, width, length, height, xAngle, yAngle, zAngle, transform.
 */
struct TransformedBoxRegion : BoxRegion
{
    Transform3d transformMatrix;

    TransformedBoxRegion(
        bool useXWidth = true,
        bool useYLength = true,
        bool useZHeight = true,
        bool useXAngle = true,
        bool useYAngle = true,
        bool useZAngle = true,
        bool useXWidthUnits = true,
        bool useYLengthUnits = true,
        bool useZHeightUnits = true,
        bool useXAngleUnits = true,
        bool useYAngleUnits = true,
        bool useZAngleUnits = true) :

        BoxRegion(useXWidth, useYLength, useZHeight, useXAngle, useYAngle, useZAngle,
            useXWidthUnits, useYLengthUnits, useZHeightUnits, useXAngleUnits, useYAngleUnits, useZAngleUnits)
    {
        /* II8N: For Extended Type TransformedBoxRegion. */
        Register("transformMatrix", transformMatrix, bepgettext("beApp", "Transform Matrix"));
        // Set the sort order of the transformMatrix to be sorted after the BoxRegion parameters.
        transformMatrix.sortOrder.Set(TRANSFORM_MATRIX_SORT_ORDER_NUMBER);
    }
};

/**
 * A subset of the full 3D TransformedBoxRegion only enabling properties for a
 * 2D transformed profile region with optional angle support.
 * Parameters are x, z, width, height, yAngle, transform. yAngle is optional but
 * enabled by default for this region.
 */
struct TransformedProfileRegion : ProfileRegion
{
    Transform3d transformMatrix;

    TransformedProfileRegion(bool includeYRotation = true) :
        ProfileRegion(includeYRotation)
    {
        /* II8N: For Extended Type TransformedProfileRegion. */
        Register("transformMatrix", transformMatrix, bepgettext("beApp", "Transform Matrix"));
        // Set the sort order of the transformMatrix to be sorted after the BoxRegion parameters.
        transformMatrix.sortOrder.Set(TRANSFORM_MATRIX_SORT_ORDER_NUMBER);
    }
};

/**
 * A subset of the full 3D TransformedBoxRegion only enabling properties for a
 * 2D transformed surface region with optional angle support.
 * Parameters are x, y, width, height, zAngle tranform. zAngle is optional but
 * enabled by default for this region.
*/
struct TransformedSurfaceRegion : SurfaceRegion2d
{
    Transform3d transformMatrix;

    TransformedSurfaceRegion(bool includeZRotation = true) :
        SurfaceRegion2d(includeZRotation)
    {
        /* II8N: For Extended Type TransformedSurfaceRegion. */
        Register("transformMatrix", transformMatrix, bepgettext("beApp", "Transform Matrix"));
        // Set the sort order of the transformMatrix to be sorted after the BoxRegion parameters.
        transformMatrix.sortOrder.Set(TRANSFORM_MATRIX_SORT_ORDER_NUMBER);
    }
};

// GOS-6026: Define a common scan mode switch from FE side.
// GOS-6833: Define proper extended type for ScanMode.
enum struct ScanModeType : k32s
{
    IMAGE = 0,
    RANGE = 1,
    PROFILE = 2,
    SURFACE = 3
};

// GOS-6833: Define scan mode as an extended type.
// Scan modes can be treated specially by clients (eg.
// to facilitate representation with icons).
struct ScanMode : Go::Params::Enum<ScanModeType>
{
    // We leave it up to the implemention to populate the
    // applicable enumerations.
    ScanMode()
    {
        this->SetExtendedType("ScanMode");
    }
};

struct CodeArea : Group
{
    Go::Params::Value<std::string> code;
    Go::Params::Value<std::vector<k64f>> errors;

    CodeArea(const std::string& format = "",
             const std::string& extendedTypeStr = "CodeArea") :
        Group(extendedTypeStr)
    {
        Register("code", code);
        // Requested by FE - Don't register errors array for now
        // as discussion of how it should be supported is not finalized.
        // Register("errors", errors);

        code.EnableFormat().SetFormat(format);
    }
};

// GOS-13265: Base class for both Anchors and KeyboardShortcut structures because
// they both share a lot of similarities.
struct AnchorBase : Group
{
    Go::Params::Value<std::string> anchorType;
    Go::Params::Value<std::string> url;

protected:
    // Don't allow this base structure to be instantiated
    // independently. It should be instantiated as part of
    // instantiating its derived structures.
    AnchorBase(const std::string& extendedTypeStr = "") : 
        Group(extendedTypeStr)
    {
        Register("anchorType", anchorType);
        Register("url", url);

        anchorType.EnableReadOnly().readOnly = true;
        url.EnableReadOnly().readOnly = true;

        // GOS-13585: Anchor url should never be copied. Common use case is tool cloning;
        // the cloned tool has a unique action url prefix that would be overwritten by that of the original.
        url.SetNeverCopy(true);
    }

    ~AnchorBase() = default;
};

// GOS-4123: Parameter action links and Tool and Scanner action consistency improvements
// Used for ArchorType parameters. Perdetermined values can be stored in the 'values'
// object and dynamically registered when required.
struct Anchor : AnchorBase
{
    std::shared_ptr<Go::Params::ParamSet> values;
    Go::Params::Value<std::string> text;
    Go::Params::Value<std::string> icon;

    Anchor() : AnchorBase("Anchor")
    {
        Register("text", text);
        Register("icon", icon);

        text.EnableReadOnly().readOnly = true;
        icon.EnableReadOnly().readOnly = true;
    }

    ~Anchor()
    {
        DeregisterParameters();
    }

    void RegisterParameters(std::shared_ptr<Go::Params::ParamSet> parameters)
    {
        Deregister("values");

        if (parameters)
        {
            this->values = parameters;
            Register("values", *parameters);
        }
    }

    void DeregisterParameters()
    {
        if (values)
        {
            Deregister("values");
            values.reset();
        }
    }
};

// GOS-9739: Implement "File" Extended Type
// Extended type for use with upload/download of binary data
struct File : Group
{
    Go::Params::Value<std::string> filename;
    Go::Params::Value<std::vector<std::string>> extensions;
    Go::Params::Value<Go::Properties::ByteVector> data;

    File(const std::vector<std::string>& exts = {}) : Group("File"), extensions(exts)
    {
        Register("filename", filename);
        Register("extensions", extensions);
        Register("data", data);
    }
};

/**
 * This structure is an updated version of the "File" type above to be used for upload & download commands.
 *
 * Added in GOS-12089.
 */
struct FileV2 : Group
{
    Go::Params::BinaryFileContents data;
    Go::Params::Value<std::string> filename;

    FileV2(const std::string& currentMimeType = "", const std::vector<std::string>& acceptedFileTypes = {}) :
        Group("FileV2")
    {
        Register("data", data);
        Register("filename", filename);

        if (!currentMimeType.empty())
        {
            data.EnableContentMediaType().SetContentMediaType(currentMimeType);
        }

        if (!acceptedFileTypes.empty())
        {
            data.EnableAcceptedFileTypes().SetAcceptedFileTypes(acceptedFileTypes);
        }
    }

    FileV2(const std::vector<std::string>& acceptedFileTypes) :
        FileV2("", acceptedFileTypes)
    {
    }
};

/**
 * This is an array of FileV2 structures to be used for multi-file upload.
 *
 * Added in GOS-12089.
 */
struct MultiFile : ConsistentArray<FileV2>
{
    MultiFile(const std::vector<std::string>& acceptedFileTypes = {}) :
        ConsistentArray<FileV2>(std::make_unique<FileV2>(acceptedFileTypes))
    {
        this->EnableMinItems().SetMinItems(1);
        this->SetExtendedType("MultiFile");
    }
};

// Base structure of an object array that table entries should inherit from
// as it contains common properties all data objects should have.
struct TableDataBase : Group
{
    Go::Params::Value<bool> isSelected;
    Go::Params::Array<Anchor> actions;

    TableDataBase()
    {
        Register("actions", actions);

        isSelectedEnabled = false;
        EnableIsSelected(true);
    }

    void EnableIsSelected(bool enable)
    {
        if (enable != isSelectedEnabled)
        {
            isSelectedEnabled = enable;

            if (enable)
            {
                Register("isSelected", isSelected);
            }
            else
            {
                Deregister("isSelected");
            }
        }
    }
private:
    bool isSelectedEnabled;
};

template <typename T>
struct TabularCollection : Group
{
    // On construction, activeRow is registered but activeColumn is not.
    Go::Params::Value<k32s> activeRow;
    Go::Params::Value<k32s> activeColumn;

    Go::Params::Array<Anchor> actions;
    // GOS-13141: Added isAllSelected attribute. If true, all data elements are selected.
    // This supercedes the isSelected attribute of TableDataBase.
    Go::Params::Value<bool> isAllSelected;
    Go::Params::Array<T> data;

    TabularCollection() : Group("TabularCollection")
    {
        Register("activeRow", activeRow);
        Register("actions", actions);
        Register("data", data);

        isAllSelected.Set(false);

        // Per-row is selectable by default. Select All is not enabled by default.
        isSelectedEnabled = true;
        isAllSelectedEnabled = false;
        activeColumnEnabled = false;

        // Track when isAllSelected changes in order to also change each child's isSelected attribute.
        isAllSelected.OnUpdatedEvent().AddListener(std::bind(&TabularCollection::OnIsAllSelectedChanged, this));
        data.OnAddedEvent().AddListener(std::bind(&TabularCollection::OnDataChildAdded, this, std::placeholders::_2));
    }

    template <typename... Args>
    void AddRow(Args&&... args)
    {
        T& newRow = data.Emplace(std::forward<Args>(args)...);

        // Sync whether or not the row should be selectable.
        newRow.EnableIsSelected(isSelectedEnabled);

        // Do not want to automatically select the row, so set isAllSelected to false upon adding a row.
        isAllSelected.Set(false);
    }

    bool IsAllSelectedEnabled()
    {
        return isAllSelectedEnabled;
    }

    bool IsSelectedEnabled()
    {
        return isSelectedEnabled;
    }

    void EnableIsAllSelected(bool enable)
    {
        if (enable != isAllSelectedEnabled)
        {
            isAllSelectedEnabled = enable;

            if (enable)
            {
                Register("isAllSelected", isAllSelected, "Select All");
            }
            else
            {
                Deregister("isAllSelected");
            }
        }
    }

    void EnableIsSelected(bool enable)
    {
        if (enable != isSelectedEnabled)
        {
            isSelectedEnabled = enable;

            for (size_t idx = 0; idx < data.Count(); idx++)
            {
                TableDataBase& dataEle = data.At(idx);
                dataEle.EnableIsSelected(enable);
            }
        }
    }

    void EnableActiveColumn(bool enable)
    {
        if (enable != activeColumnEnabled)
        {
            activeColumnEnabled = enable;

            if (enable)
            {
                Register("activeColumn", activeColumn);
            }
            else
            {
                Deregister("activeColumn");
            }
        }
    }

protected:
    // Only allow T to be a subclass of TableDataBase. This way, isSelected will always be available.
    static_assert(std::is_base_of<TableDataBase, T>::value, "T must be a descendant type of TableDataBase.");

    bool isAllSelectedEnabled;
    bool isSelectedEnabled;
    bool activeColumnEnabled;
private:

    // Event triggers when isAllSelected changes.
    void OnIsAllSelectedChanged()
    {
        if (isAllSelectedEnabled)
        {
            for (size_t idx = 0; idx < data.Count(); idx++)
            {
                data.At(idx).isSelected.Set(isAllSelected.Get());
            }
        }
    }

    // Event triggers when a child's isSelected changes.
    void OnDataSelectedChanged(Go::Properties::Value<bool>& changedIsSelected)
    {
        if (isAllSelectedEnabled)
        {
            // If isAllSelected is true and a data element was toggled to false,
            // isAllSelected should also toggle to false.
            if (isAllSelected.Get() && !changedIsSelected.Get())
            {
                isAllSelected.Set(false);
            }
        }
    }

    // Event triggers when an element is added to data.
    // Don't need to track when it's removed as it will never notify again.
    void OnDataChildAdded(T& newChild)
    {
        newChild.isSelected.OnUpdatedEvent().AddListener(std::bind(&TabularCollection::OnDataSelectedChanged, this, std::placeholders::_1));
    }
};

// GOS-9628: Image support in JSON and Property Grid component (including in table columns)
// Extended type for encapsulating image/thumbnail data
struct Image : Group
{
    Go::Params::Value<Go::Properties::ByteVector> data;
    Go::Params::Value<k16u> displayWidth;
    Go::Params::Value<k16u> displayHeight;
    Go::Params::Value<std::string> contentMediaType;

    Image(const std::string& mediaType = {}) : Group("Image"), contentMediaType(mediaType)
    {
        this->EnableReadOnly();
        this->readOnly = true;

        Register("data", data);
        Register("displayWidth", displayWidth);
        Register("displayHeight", displayHeight);

        data.RegisterSchema("contentMediaType", contentMediaType);
    }
};

}}

// Common definitions for common aliases.
using ParamsUniquePtr = std::unique_ptr<Go::Params::ParamSet>;
using ParamsUniqueConstPtr = std::unique_ptr<const Go::Params::ParamSet>;
using ParamsSharedPtr = std::shared_ptr<Go::Params::ParamSet>;
using ParamsSharedConstPtr = std::shared_ptr<const Go::Params::ParamSet>;
using ParamsWeakPtr = std::weak_ptr<Go::Params::ParamSet>;
using ParamsWeakConstPtr = std::weak_ptr<const Go::Params::ParamSet>;

// This is the one already used everywhere, so just keep the existing name.
using ParamsPtr = ParamsSharedPtr;
#endif
