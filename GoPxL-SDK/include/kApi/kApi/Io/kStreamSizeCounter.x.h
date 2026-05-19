/**
 * @file    kStreamSizeCounter.x.h
 * @brief   Declares the kStreamSizeCounter class.
 *
 * @internal
 * Copyright (C) 2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef K_API_STREAM_SIZE_COUNTER_X_H
#define K_API_STREAM_SIZE_COUNTER_X_H

#include <kApi/Io/kStream.h>

typedef struct kStreamSizeCounterClass
{
    kStreamClass base;
    k64u position;
    k64u length;
} kStreamSizeCounterClass;

kDeclareClassEx(k, kStreamSizeCounter, kStream)

/*
* Private methods.
*/

kFx(kStatus) xkStreamSizeCounter_Init(kStreamSizeCounter counter, kType type, kAlloc allocator);
kFx(kStatus) xkStreamSizeCounter_VRelease(kStreamSizeCounter counter);

kFx(kStatus) xkStreamSizeCounter_VReadSomeImpl(kStreamSizeCounter counter, void* buffer, kSize minCount, kSize maxCount, kSize* bytesRead);
kFx(kStatus) xkStreamSizeCounter_VWriteImpl(kStreamSizeCounter counter, const void* buffer, kSize size);
kFx(kStatus) xkStreamSizeCounter_VSeek(kStreamSizeCounter counter, k64s offset, kSeekOrigin origin);
kFx(kStatus) xkStreamSizeCounter_VFlush(kStreamSizeCounter counter);
kFx(kStatus) xkStreamSizeCounter_VFill(kStreamSizeCounter counter);

#endif
