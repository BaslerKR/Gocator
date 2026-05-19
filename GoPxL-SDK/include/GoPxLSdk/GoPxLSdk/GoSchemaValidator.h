/**
 * @file    GoSchemaValidator.h
 * @brief   Declares the GoPxLSdk.GoSchemaValidator utility class.
 *
 * @internal
 * Copyright (C) 2024-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_SCHEMAVALIDATOR_H
#define GO_PXL_SDK_SCHEMAVALIDATOR_H

#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoJson.h>
#include <string>
#include <vector>

namespace GoPxLSdk
{

/**
 * Static utility class for JSON Schema traversal and validation.
 *
 * Provides type-aware schema navigation (SchemaForPath) and assertion keyword
 * checking (Validate) against Gocator resource schemas.
 *
 * @version               Introduced in 1.5.0.0
 */
class GoPxLSdkClass GoSchemaValidator
{
public:
    /**
     * Navigates into a JSON Schema to find the sub-schema for a given property path.
     *
     * Performs type-aware traversal:
     * - Object nodes: navigates via properties/<segment>
     * - Array nodes with tuple items (items is array): navigates via items/<index>
     * - Array nodes with list items (items is object): navigates via items (skips index)
     *
     * @public                              @memberof GoSchemaValidator
     * @version                             Introduced in 1.5.0.0
     * @param fullSchema                    The root _schema object from the resource response.
     * @param path                          JSON pointer path (e.g., "/parameters/scanMode" or "parameters/scanMode").
     *                                      Empty string returns fullSchema unchanged.
     * @return                              The sub-schema for the given path.
     * @throws GoPxLSdk::GoResourceError    If the path cannot be resolved in the schema.
     */
    static GoJson SchemaForPath(const GoJson& fullSchema, const std::string& path);

    /**
     * Validates a JSON value against a schema node.
     *
     * Checks the following assertion keywords (in order):
     * 1. readOnly - blocks writes to read-only properties
     * 2. type - verifies JSON type matches schema type
     * 3. minimum, maximum, exclusiveMinimum, exclusiveMaximum, multipleOf (numeric)
     * 4. minLength, maxLength (string)
     * 5. minItems, maxItems (array)
     * 6. required (object)
     * 7. enum (any primitive)
     *
     * Short-circuits on readOnly (write is categorically rejected) and on type
     * mismatch (further type-specific assertions are meaningless). For all other
     * checks, errors are accumulated without short-circuiting.
     *
     * @public                              @memberof GoSchemaValidator
     * @version                             Introduced in 1.5.0.0
     * @param value                         The JSON value to validate.
     * @param schemaNode                    The schema node to validate against.
     * @param errors  [out]                 Accumulates human-readable error descriptions.
     * @return                              True if validation passes, false otherwise.
     */
    static bool Validate(const GoJson& value, const GoJson& schemaNode,
                         std::vector<std::string>& errors);

private:
    GoSchemaValidator() = delete;
};

}

#endif
