/** 
 * @file    kImage.x.h
 *
 * @internal
 * Copyright (C) 2003-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef K_API_IMAGE_X_H
#define K_API_IMAGE_X_H

#define xkIMAGE_ALIGNMENT       (3)          //align rows to 8-byte boundaries by default

typedef struct kImageClass
{
    kObjectClass base; 
    kAlloc dataAlloc;           //allocator used for data
    kType pixelType;            //item type
    kSize pixelSize;            //item size, in bytes
    kSize allocSize;            //size of allocated image memory, in bytes
    void* pixels;               //image memory 
    kSize stride;               //image stride (row size), in bytes
    kSize width;                //image width, in pixels
    kSize height;               //image height, in pixels
    kBool isAttached;           //is image memory externally owned?
    kPixelFormat format;        //optional pixel format descriptor
    kCfa cfa;                   //optional color filter array type
    kMemoryAlignment alignment; //alignment for the image data
} kImageClass;
    
kDeclareClassEx(k, kImage, kObject) 

/*
 * Deprecated functions (use: kPixelFormat.h).
 * In new code, avoid setting/getting individual pixels. Addressing an entire row is often okay/efficient.
 * Beware: each kPixelFormat has its own peculiarities. At runtime, the kImage class just provides a 
 * contiguous buffer, large enough to contain all pixel data. Serialized data may not necessarily be 
 * identical to how it's placed in CPU/GPU memory.
 */
kFx(kType) kImage_PixelType(kImage image);
kFx(kSize) kImage_PixelSize(kImage image);
kFx(kSize) kImage_Area(kImage image); /* area is an ambiguous term/concept */
kFx(kStatus) kImage_SetPixel(kImage image, kSize x, kSize y, const void* pixel);
kFx(kStatus) kImage_Pixel(kImage image, kSize x, kSize y, void* pixel);
kFx(kStatus) kImage_SetPixelFormat(kImage image, kPixelFormat format); /* the pixel format should be treated read-only */
kFx(kStatus) kImage_SetCfa(kImage image, kCfa cfa); /* CFA is implied by pixel format and should be treated read-only */
kFx(kCfa) kImage_Cfa(kImage image);
kInlineFx(void*) kImage_DataAt(kImage image, kSSize x, kSSize y);
kInlineFx(void*) kImage_At(kImage image, kSize x, kSize y);

/* 
* Forward declarations. 
*/

kFx(kStatus) kImage_Attach(kImage image, void* pixels, kType pixelType, kSize width, kSize height, kSize stride); 
kInlineFx(kSize) kImage_DataSize(kImage image);
kInlineFx(kSize) kImage_Width(kImage image);
kInlineFx(kSize) kImage_Height(kImage image);
kInlineFx(void*) kImage_Data(kImage image);
kInlineFx(void*) kImage_RowAt(kImage image, kSize y);

/* 
* Private methods. 
*/

kFx(kStatus) xkImage_ConstructFramework(kImage* image, kAlloc objectAlloc);

kFx(kStatus) xkImage_Init(kImage image, kType classType, kType pixelType, kPixelFormat format,
    kSize width, kSize height, kAlloc alloc, kAlloc valueAlloc, kMemoryAlignment alignment);

kFx(kStatus) xkImage_VClone(kImage image, kImage source, kAlloc valueAlloc, kObject context); 

kFx(kStatus) xkImage_VRelease(kImage image); 

kInlineFx(kSize) xkImage_VSize(kImage image)
{
    kObj(kImage, image); 
    kSize dataSize = (!obj->isAttached) ? obj->allocSize : kImage_DataSize(image); 
    kSize size = sizeof(kImageClass) + dataSize; 

    return size; 
}

kFx(kAllocTrait) xkImage_VAllocTraits(kImage image);

kFx(kStatus) xkImage_WriteDat5V3(kImage image, kSerializer serializer); 
kFx(kStatus) xkImage_ReadDat5V3(kImage image, kSerializer serializer); 
kFx(kStatus) xkImage_WriteDat6V0(kImage image, kSerializer serializer); 
kFx(kStatus) xkImage_ReadDat6V0(kImage image, kSerializer serializer); 

#if defined(K_CPP)
kFx(kStatus) kImage_ConstructEx(kImage* image, kType pixelType, kPixelFormat format, kSize width, kSize height, kAlloc allocator, kAlloc valueAllocator, kMemoryAlignment valueAlignment = kALIGN_ANY);
#endif

kFx(kStatus) kImage_AllocateEx(kImage image, kType pixelType, kPixelFormat format, kSize width, kSize height);
kFx(kStatus) kImage_AttachEx(kImage image, void* pixels, kType pixelType, kPixelFormat format, kSize width, kSize height, kSize stride);


//kArrayProvider
kFx(kStatus) xkImage_ConstructDefaultEx(kImage* image, kAlloc objectAlloc, kAlloc valueAlloc);
kFx(kStatus) xkImage_Imitate(kImage image, kImage source);

kFx(kStatus) xkImage_Assign(kImage image, kImage source, kObject context);
kFx(kStatus) xkImage_CopyPixels(kImage image, kImage source, kObject context); 

#define kImage_SetPixelT(kImage_image, kSize_x, kSize_y, TPtr_pixel) \
    xkImage_SetPixelT(kImage_image, kSize_x, kSize_y, TPtr_pixel, sizeof(*(TPtr_pixel)))

#define kImage_PixelT(kImage_image, kSize_x, kSize_y, TPtr_pixel) \
    xkImage_PixelT(kImage_image, kSize_x, kSize_y, TPtr_pixel, sizeof(*(TPtr_pixel)))

#define kImage_SetAsT(kImage_image, kSize_x, kSize_y, T_value, T) \
    (kPointer_WriteAs(xkImage_AsT(kImage_image, kSize_x, kSize_y, sizeof(T)), T_value, T), (void)0)

#define kImage_AsT(kImage_image, kSize_x, kSize_y, T) \
    kPointer_ReadAs(xkImage_AsT(kImage_image, kSize_x, kSize_y, sizeof(T)), T)

kInlineFx(kStatus) xkImage_AttachT(kImage image, void* pixels, kType pixelType, kSize width, kSize height, kSize stride, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(pixelType, itemSize)); 

    return kImage_Attach(image, pixels, pixelType, width, height, stride); 
} 

kInlineFx(kStatus) xkImage_SetPixelT(kImage image, kSize x, kSize y, const void* pixel, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_SetPixel(image, x, y, pixel);
} 

kInlineFx(kStatus) xkImage_PixelT(kImage image, kSize x, kSize y, void* pixel, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_Pixel(image, x, y, pixel);
} 

kInlineFx(void*) xkImage_DataT(kImage image, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_Data(image);
} 

kInlineFx(void*) xkImage_DataAtT(kImage image, kSSize x, kSSize y, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_DataAt(image, x, y);
} 

kInlineFx(void*) xkImage_AtT(kImage image, kSize x, kSize y, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_At(image, x, y);
} 

kInlineFx(void*) xkImage_RowAtT(kImage image, kSize y, kSize itemSize)
{
    kAssert(xkType_IsPointerCompatible(kImage_PixelType(image), itemSize)); 

    return kImage_RowAt(image, y);
} 

kInlineFx(void*) xkImage_AsT(kImage image, kSize x, kSize y, kSize itemSize)
{
    kObj(kImage, image); 
    kAssert(itemSize == obj->pixelSize); 

    return kImage_At(image, x, y);
}

#endif
