/**
 * @file    kStreamSizeCounter.h
 * @brief   Declares the kStreamSizeCounter class.
 *
 * @internal
 * Copyright (C) 2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef K_API_STREAM_SIZE_COUNTER_H
#define K_API_STREAM_SIZE_COUNTER_H

#include <kApi/kApiDef.h>
#include <kApi/Io/kStreamSizeCounter.x.h>

/**
 * @class   kStreamSizeCounter
 * @extends kStream
 * @ingroup kApi-Io
 * @brief   Represents a write-only stream that counts bytes written without storing data.
 *
 * kStreamSizeCounter is useful for calculating the serialized size of data before actually
 * writing it. It implements the kStream interface but discards all written data,
 * only tracking the total number of bytes that would have been written.
 */
//typedef kStream kStreamSizeCounter;

/**
 * Constructs a kStreamSizeCounter object.
 *
 * @public              @memberof kStreamSizeCounter
 * @param   counter     Destination for the constructed object handle.
 * @param   allocator   Memory allocator (or kNULL for default).
 * @return              Operation status.
 */
kFx(kStatus) kStreamSizeCounter_Construct(kStreamSizeCounter* counter, kAlloc allocator);

/**
 * Returns the current length (total bytes written) of the counter.
 *
 * @public              @memberof kStreamSizeCounter
 * @param   counter     kStreamSizeCounter object.
 * @return              Total bytes written to the counter.
 */
kFx(k64u) kStreamSizeCounter_Length(kStreamSizeCounter counter);

/**
 * Returns the current position of the write pointer, relative to the beginning.
 *
 * @public              @memberof kStreamSizeCounter
 * @param   counter     kStreamSizeCounter object.
 * @return              Position of the write pointer.
 */
kFx(k64u) kStreamSizeCounter_Position(kStreamSizeCounter counter);

/**
 * Resets the counter to zero.
 *
 * @public              @memberof kStreamSizeCounter
 * @param   counter     kStreamSizeCounter object.
 * @return              Operation status.
 */
kFx(kStatus) kStreamSizeCounter_Reset(kStreamSizeCounter counter);

#endif
