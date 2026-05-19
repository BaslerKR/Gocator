/** 
 * @file    kPixelFormat.x.h
 *
 * @internal
 * Copyright (C) 2005-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef K_API_PIXEL_FORMAT_X_H
#define K_API_PIXEL_FORMAT_X_H

#define kPIXEL_FORMAT_NULL                      (0)                                                 ///< Legacy: Unknown pixel format.
#define kPIXEL_FORMAT_8BPP_GREYSCALE            (1)                                                 ///< Legacy: 8-bit greyscale (k8u).
#define kPIXEL_FORMAT_8BPP_CFA                  (2)                                                 ///< Legacy: 8-bit color filter array (k8u).
#define kPIXEL_FORMAT_8BPC_BGRX                 (3)                                                 ///< Legacy: 8-bits-per-channel color with 4 channels (blue/green/red/unused)(kRgb).
#define kPIXEL_FORMAT_1BPP_GREYSCALE            (4)                                                 ///< Legacy: 1-bit greyscale, 8 packed pixels per image element (k8u).
#define kPIXEL_FORMAT_16BPP_GREYSCALE           (5)                                                 ///< Legacy: 16-bit greyscale (k16u).
#define kPIXEL_FORMAT_8BPC_BGR                  (6)                                                 ///< Legacy: 8-bits-per-channel color with 3 channels (blue/green/red)(kRgb24).

#define kPIXEL_FORMAT_MONO10_PACKED             (0x010C0004)                                        ///< GigE Vision: Monochrome 10-bit packed.
#define kPIXEL_FORMAT_MONO12_PACKED             (0x010C0006)                                        ///< GigE Vision: Monochrome 12-bit packed.

#define kPIXEL_FORMAT_BAYER_BG12_PACKED         (0x010C002D)                                        ///< GigE Vision: Bayer Blue-Green 12-bit packed.
#define kPIXEL_FORMAT_BAYER_GB12_PACKED         (0x010C002C)                                        ///< GigE Vision: Bayer Green-Blue 12-bit packed.
#define kPIXEL_FORMAT_BAYER_GR12_PACKED         (0x010C002A)                                        ///< GigE Vision: Bayer Green-Red 12-bit packed.
#define kPIXEL_FORMAT_BAYER_RG12_PACKED         (0x010C002B)                                        ///< GigE Vision: Bayer Red-Green 12-bit packed.

#define kPIXEL_FORMAT_MODERN_TYPE_START         kPIXEL_FORMAT_MONO1p                                ///< Non-legacy start: any kPixelFormat lower than this value is legacy.

kDeclareEnumEx(k, kPixelFormat, kValue)

kFx(kBool) kPixelFormat_IsModern(kPixelFormat pixelFormat);
kFx(kBool) kPixelFormat_IsLegacy(kPixelFormat pixelFormat);
kFx(kBool) kPixelFormat_IsPacked(kPixelFormat pixelFormat);

kFx(kBool) xkPixelFormat_VEquals(kType type, const void* value, const void* other);
kFx(kSize) xkPixelFormat_VHashCode(kType type, const void* value);
kFx(kStatus) xkPixelFormat_Write(kType type, const void* values, kSize count, kSerializer serializer);
kFx(kStatus) xkPixelFormat_Read(kType type, void* values, kSize count, kSerializer serializer);

kFx(kSize) xkPixelFormat_Stride(kPixelFormat format, kSize width);
kFx(kSize) xkPixelFormat_Width(kPixelFormat format, kSize stride);

kInlineFx(kStatus) kPixelFormat_Format(kPixelFormat format, kChar* text, kSize capacity)
{
    return kPixelFormat_Name(format, text, capacity);
}

#endif
