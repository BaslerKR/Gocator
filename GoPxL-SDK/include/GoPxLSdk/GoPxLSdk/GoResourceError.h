/**
 * @file    GoResourceError.h
 * @brief   Declares the GoPxLSdk.GoResourceError class.
 *
 * @internal
 * Copyright (C) 2024-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_RESOURCEERROR_H
#define GO_PXL_SDK_RESOURCEERROR_H

#include <stdexcept>
#include <string>
#include <vector>

namespace GoPxLSdk
{

class GoPxLSdkClass GoResourceError : public std::runtime_error
{
public:
    /**
     * Constructs GoResourceError with a default message.
     *
     * @public                @memberof GoResourceError
     * @version               Introduced in 1.5.0.0
     */
    GoResourceError();

    /**
     * Constructs GoResourceError with the given message.
     *
     * @public                @memberof GoResourceError
     * @version               Introduced in 1.5.0.0
     * @param message         Description of the error.
     */
    GoResourceError(const std::string& message);
};

/**
 * Thrown when client-side schema validation fails on a GoResource setter.
 *
 * Contains one or more human-readable error descriptions accessible via Errors().
 *
 * @version               Introduced in 1.5.0.0
 */
class GoPxLSdkClass GoResourceValidationError : public GoResourceError
{
public:
    /**
     * Constructs GoResourceValidationError with a summary message and detailed errors.
     *
     * @public                @memberof GoResourceValidationError
     * @version               Introduced in 1.5.0.0
     * @param message         Summary description of the validation failure.
     * @param errors          Individual validation error descriptions.
     */
    GoResourceValidationError(const std::string& message,
                              const std::vector<std::string>& errors);

    /**
     * Gets the individual validation error descriptions.
     *
     * @public                @memberof GoResourceValidationError
     * @version               Introduced in 1.5.0.0
     * @return                The validation error strings.
     */
    const std::vector<std::string>& Errors() const;

private:
    std::vector<std::string> errors_;
};

}

#endif
