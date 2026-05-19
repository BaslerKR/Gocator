/** 
 * @file    kPixelFormat.h
 * @brief   Declares the kPixelFormat class. 
 *
 * @internal
 * Copyright (C) 2005-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef K_API_PIXEL_FORMAT_H
#define K_API_PIXEL_FORMAT_H

#include <kApi/kApiDef.h>

/**
 * @struct  kPixelFormat
 * @extends kValue
 * @ingroup kApi-Data  
 * @brief   Pixel format descriptor.
 *
 * kPixelFormat supports the kdat6 serialization protocol.
 * Values reflect Pixel Format Naming Convention (like GvPixelFormat.h and PFNC.h).
 */
typedef xkPixelFormat kPixelFormat; 

/** @relates kPixelFormat @{ */
#define kPIXEL_FORMAT_MONO1p                    (0x01010037)                                        ///< Monochrome, 1-bit.
#define kPIXEL_FORMAT_MONO2p                    (0x01020038)                                        ///< Monochrome, 2-bit.
#define kPIXEL_FORMAT_MONO4p                    (0x01040039)                                        ///< Monochrome, 4-bit.
#define kPIXEL_FORMAT_MONO8                     (0x01080001)                                        ///< Monochrome, 8-bit.
#define kPIXEL_FORMAT_MONO10                    (0x01100003)                                        ///< Monochrome, 10-bit.
#define kPIXEL_FORMAT_MONO10p                   (0x010A0046)                                        ///< Monochrome, 10-bit packed.
#define kPIXEL_FORMAT_MONO12                    (0x01100005)                                        ///< Monochrome, 12-bit.
#define kPIXEL_FORMAT_MONO12p                   (0x010C0047)                                        ///< Monochrome, 12-bit packed.
#define kPIXEL_FORMAT_MONO14                    (0x01100025)                                        ///< Monochrome, 14 bit.
#define kPIXEL_FORMAT_MONO14p                   (0x010E0104)                                        ///< Monochrome, 14 bit packed.
#define kPIXEL_FORMAT_MONO16                    (0x01100007)                                        ///< Monochrome, 16 bit.
#define kPIXEL_FORMAT_MONO32                    (0x01200111)                                        ///< Monochrome, 32 bit.

#define kPIXEL_FORMAT_DATA8                     (0x01080116)                                        ///< Data, 8-bit.
#define kPIXEL_FORMAT_DATA8s                    (0x01080117)                                        ///< Data, 8-bit signed.
#define kPIXEL_FORMAT_DATA16                    (0x01100118)                                        ///< Data, 16-bit.
#define kPIXEL_FORMAT_DATA16s                   (0x01100119)                                        ///< Data, 16-bit signed.
#define kPIXEL_FORMAT_DATA32                    (0x0120011A)                                        ///< Data, 32-bit.
#define kPIXEL_FORMAT_DATA32s                   (0x0120011B)                                        ///< Data, 32-bit signed.
#define kPIXEL_FORMAT_DATA64                    (0x0140011D)                                        ///< Data, 64-bit.
#define kPIXEL_FORMAT_DATA64s                   (0x0140011E)                                        ///< Data, 64-bit signed.
#define kPIXEL_FORMAT_DATA32f                   (0x0120011C)                                        ///< Data, 32-bit floating point.
#define kPIXEL_FORMAT_DATA64f                   (0x0140011F)                                        ///< Data, 64-bit floating point.

#define kPIXEL_FORMAT_BAYER_BG8                 (0x0108000B)                                        ///< Bayer, Blue-Green 8-bit.
#define kPIXEL_FORMAT_BAYER_BG10                (0x0110000F)                                        ///< Bayer, Blue-Green 10-bit unpacked.
#define kPIXEL_FORMAT_BAYER_BG10p               (0x010A0052)                                        ///< Bayer, Blue-Green 10-bit packed.
#define kPIXEL_FORMAT_BAYER_BG12                (0x01100013)                                        ///< Bayer, Blue-Green 12-bit unpacked.
#define kPIXEL_FORMAT_BAYER_BG12p               (0x010C0053)                                        ///< Bayer, Blue-Green 12-bit packed.
#define kPIXEL_FORMAT_BAYER_BG16                (0x01100031)                                        ///< Bayer, Blue-Green 16-bit.
#define kPIXEL_FORMAT_BAYER_GB8                 (0x0108000A)                                        ///< Bayer, Green-Blue 8-bit.
#define kPIXEL_FORMAT_BAYER_GB10                (0x0110000E)                                        ///< Bayer, Green-Blue 10-bit unpacked.
#define kPIXEL_FORMAT_BAYER_GB10p               (0x010A0054)                                        ///< Bayer, Green-Blue 10-bit packed.
#define kPIXEL_FORMAT_BAYER_GB12                (0x01100012)                                        ///< Bayer, Green-Blue 12-bit unpacked.
#define kPIXEL_FORMAT_BAYER_GB12p               (0x010C0055)                                        ///< Bayer, Green-Blue 12-bit packed.
#define kPIXEL_FORMAT_BAYER_GB16                (0x01100030)                                        ///< Bayer, Green-Blue 16-bit.
#define kPIXEL_FORMAT_BAYER_GR8                 (0x01080008)                                        ///< Bayer, Green-Red 8-bit.
#define kPIXEL_FORMAT_BAYER_GR10                (0x0110000C)                                        ///< Bayer, Green-Red 10-bit unpacked.
#define kPIXEL_FORMAT_BAYER_GR10p               (0x010A0056)                                        ///< Bayer, Green-Red 10-bit packed.
#define kPIXEL_FORMAT_BAYER_GR12                (0x01100010)                                        ///< Bayer, Green-Red 12-bit unpacked.
#define kPIXEL_FORMAT_BAYER_GR12p               (0x010C0057)                                        ///< Bayer, Green-Red 12-bit packed.
#define kPIXEL_FORMAT_BAYER_GR16                (0x0110002E)                                        ///< Bayer, Green-Red 16-bit.
#define kPIXEL_FORMAT_BAYER_RG8                 (0x01080009)                                        ///< Bayer, Red-Green 8-bit.
#define kPIXEL_FORMAT_BAYER_RG10                (0x0110000D)                                        ///< Bayer, Red-Green 10-bit unpacked.
#define kPIXEL_FORMAT_BAYER_RG10p               (0x010A0058)                                        ///< Bayer, Red-Green 10-bit packed.
#define kPIXEL_FORMAT_BAYER_RG12                (0x01100011)                                        ///< Bayer, Red-Green 12-bit unpacked.
#define kPIXEL_FORMAT_BAYER_RG12p               (0x010C0059)                                        ///< Bayer, Red-Green 12-bit packed.
#define kPIXEL_FORMAT_BAYER_RG16                (0x0110002F)                                        ///< Bayer, Red-Green 16-bit.

#define kPIXEL_FORMAT_RGBa8                     (0x02200016)                                        ///< RGBA, 8-bit.
#define kPIXEL_FORMAT_RGBa10                    (0x0240005F)                                        ///< RGBA, 10-bit.
#define kPIXEL_FORMAT_RGBa12                    (0x02400061)                                        ///< RGBA, 12-bit.
#define kPIXEL_FORMAT_RGBa14                    (0x02400063)                                        ///< RGBA, 14-bit.
#define kPIXEL_FORMAT_RGBa16                    (0x02400064)                                        ///< RGBA, 16-bit.

#define kPIXEL_FORMAT_RGB8                      (0x02180014)                                        ///< RGB, 8-bit.
#define kPIXEL_FORMAT_RGB10                     (0x02300018)                                        ///< RGB, 10-bit.
#define kPIXEL_FORMAT_RGB12                     (0x0230001A)                                        ///< RGB, 12-bit.
#define kPIXEL_FORMAT_RGB14                     (0x0230005E)                                        ///< RGB, 14-bit.
#define kPIXEL_FORMAT_RGB16                     (0x02300033)                                        ///< RGB, 16-bit.

#define kPIXEL_FORMAT_BGRa8                     (0x02200017)                                        ///< BGRA, 8-bit.
#define kPIXEL_FORMAT_BGRa10                    (0x0240004C)                                        ///< BGRA, 10-bit.
#define kPIXEL_FORMAT_BGRa12                    (0x0240004E)                                        ///< BGRA, 12-bit.
#define kPIXEL_FORMAT_BGRa14                    (0x02400050)                                        ///< BGRA, 14-bit.
#define kPIXEL_FORMAT_BGRa16                    (0x02400051)                                        ///< BGRA, 16-bit.

#define kPIXEL_FORMAT_BGR8                      (0x02180015)                                        ///< BGR, 8-bit.
#define kPIXEL_FORMAT_BGR10                     (0x02300019)                                        ///< BGR, 10-bit.
#define kPIXEL_FORMAT_BGR12                     (0x0230001B)                                        ///< BGR, 12-bit.
#define kPIXEL_FORMAT_BGR14                     (0x0230004A)                                        ///< BGR, 14-bit.
#define kPIXEL_FORMAT_BGR16                     (0x0230004B)                                        ///< BGR, 16-bit.
/** @} */

/** 
 * Formats the pixel format name as a string. 
 * 
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   text        Receives formatted string.
 * @param   capacity    Capacity of the string buffer. 
 * @return              Operation status. 
 */
kFx(kStatus) kPixelFormat_Name(kPixelFormat format, kChar* text, kSize capacity);

/** 
 * Formats the pixel format descriptions as a string. 
 * 
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   text        Receives formatted string.
 * @param   capacity    Capacity of the string buffer. 
 * @return              Operation status. 
 */
kFx(kStatus) kPixelFormat_Description(kPixelFormat format, kChar* text, kSize capacity);

/**
 * Returns number of bits per pixel, e.g. 8, 10 12, etc.
 *
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @return              Number of bits per pixel.
 */
kFx(kSize) kPixelFormat_PixelBits(kPixelFormat format);

/**
 * Returns stride in bytes from width (in pixels).
 *
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   width       Width.
 * @return              Stride in bytes.
 */
kFx(kSize) kPixelFormat_Stride(kPixelFormat format, kSize width);

/**
 * Returns width in pixels from stride (in bytes).
 *
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   stride      Stride.
 * @return              Width in pixels.
 */
kFx(kSize) kPixelFormat_Width(kPixelFormat format, kSize stride);

/**
 * Returns size in bytes, when data is serialized.
 *
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   width       Width.
 * @param   height      Height.
 * @return              Size in bytes.
 */
kFx(kSize) kPixelFormat_Size(kPixelFormat format, kSize width, kSize height);

/**
 * Returns size in bytes when buffer is situated in CPU/GPU memory.
 *
 * @public              @memberof kPixelFormat
 * @param   format      Pixel format.
 * @param   width       Width.
 * @param   height      Height.
 * @return              Allocation size in bytes.
 */
kFx(kSize) kPixelFormat_AllocSize(kPixelFormat format, kSize width, kSize height);

#include <kApi/Data/kPixelFormat.x.h>

#endif
