/**
 * @file    Def.h
 * @brief   Declares the general SDK definitions.
 *
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_DEF_H
#define GO_PXL_SDK_DEF_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <queue>
#include <algorithm>

#include <kApi/kApiDef.h>
#include <GoApi/GoApiDef.h>
#include <GoApi/NlohmannDeclarations.h>

#if defined(GOPXLSDK_EMIT)
#    define GoPxLSdkCppFx(TYPE)         kExportEx(TYPE)
#    define GoPxLSdkFx(TYPE)            kExportFx(TYPE)
#    define GoPxLSdkCx(TYPE)            kExportCx(TYPE)
#    define GoPxLSdkDx(TYPE)            kExportDx(TYPE)
#    define GoPxLSdkClass               kExportClass
#elif defined (GOPXLSDK_STATIC)
#    define GoPxLSdkCppFx(TYPE)         TYPE
#    define GoPxLSdkFx(TYPE)            kInFx(TYPE)
#    define GoPxLSdkCx(TYPE)            kInCx(TYPE)
#    define GoPxLSdkDx(TYPE)            kInDx(TYPE)
#    define GoPxLSdkClass
#else
#    define GoPxLSdkCppFx(TYPE)         kImportEx(TYPE)
#    define GoPxLSdkFx(TYPE)            kImportFx(TYPE)
#    define GoPxLSdkCx(TYPE)            kImportCx(TYPE)
#    define GoPxLSdkDx(TYPE)            kImportDx(TYPE)
#    define GoPxLSdkClass               kImportClass
#endif

constexpr k32u GO_PXL_SDK_DEFAULT_TCP_TIMEOUT_MILLISECONDS = 1000;
constexpr k16u GO_PXL_SDK_DEFAULT_CONTROL_PORT             = 3600;     // Could vary for each GoPxL instance.
constexpr k16u GO_PXL_SDK_DEFAULT_GDP_SERVER_PORT          = 3601;     // Could vary for each GoPxL instance.
constexpr k16u GO_PXL_SDK_DEFAULT_WEB_PORT                 = 8100;     // Could vary for each GoPxL instance.

constexpr k32u GO_PXL_SDK_MILLISECONDS_TO_MICROSECONDS_CONVERSION = 1000;


namespace GoPxLSdk
{
using Byte = kByte;
using ByteArray = std::vector<Byte>;
using SerialNum = std::string;
using ResourcePath = std::string;

}

#endif