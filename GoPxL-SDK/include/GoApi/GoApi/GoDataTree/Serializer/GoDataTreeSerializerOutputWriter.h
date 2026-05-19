/**@file    GoDataTreeSerializerOutputWriter.h
 * Defines the GoDataTreeSerializerOutputContainerWriter and GoDataTreeSerializerOutputStreamWriter classes.
 */

#ifndef GOAPI_GODATATREE_SERIALIZER_GODATATREESERIALIZEROUTPUTWRITER_H
#define GOAPI_GODATATREE_SERIALIZER_GODATATREESERIALIZEROUTPUTWRITER_H

#include <GoApi/GoDataTree/Serializer/IGoDataTreeSerializerOutputWriter.h>
#include <iomanip>

namespace GoApi
{

class GoDataTreeByteArrayChunkedWriter
{
public:
    /**
     * This function writes a byte array into the writer one chunk at a time.
     *
     * @param writer                IGoDataTreeSerializerOutputWriter object.
     * @param stream                Underlying kStream output object.
     * @param byteArray             Pointer to an array of bytes.
     * @param byteArraySize         Number of bytes in the byte array.
     * @param chunkSize             Number of bytes to write into the serializer with each
     *                              call to the serializer write API.
     */
    static void SerializeByteArrayChunked(IGoDataTreeSerializerOutputWriter& writer, kStream stream, kByte* byteArray, kSize byteArraySize, kSize chunkSize);
};

/*! @class GoDataTreeSerializerOutputContainerWriter GoDataTreeSerializerOutputWriter.h

@brief A class used to write data into std::string or std::vector.
@brief ContainerT type must support .push_back(), .insert() and .end() APIs.
@ingroup Go
*/
template <typename ContainerT>
class GoApiClass GoDataTreeSerializerOutputContainerWriter : public IGoDataTreeSerializerOutputWriter
{
public:
    /**
     * Constructs the GoDataTreeSerializerOutputContainerWriter object.
     *
     * @param output                            ContainerT pointer.
     */
    explicit GoDataTreeSerializerOutputContainerWriter(ContainerT* output);

    /* IGoDataTreeSerializerOutputWriter overrides */
    void WriteByte(kByte byte) override;
    void WriteByteArray(kByte* bytes, kSize size) override;
    void WriteByteArray(StreamFx streamingFx) override;
    void WriteTextArray(kByte* text, kSize size) override;
    void WriteNumberBytes(kByte* number, kSize size, bool bigEndian = true) override;

private:
    ContainerT* output;
};

/*! @class GoDataTreeSerializerOutputCounter GoDataTreeSerializerOutputWriter.h

@brief A class used to count bytes that serialized GoDataTree would take.
@ingroup Go
*/
class GoApiClass GoDataTreeSerializerOutputCounter : public IGoDataTreeSerializerOutputWriter
{
public:
    /**
     * Constructs the GoDataTreeSerializerOutputCounter object.
     */
    explicit GoDataTreeSerializerOutputCounter();

    /* IGoDataTreeSerializerOutputWriter overrides */
    void WriteByte(kByte) override;
    void WriteByteArray(kByte*, kSize size) override;
    void WriteByteArray(StreamFx streamingFx) override;
    void WriteTextArray(kByte* text, kSize size) override;
    void WriteNumberBytes(kByte*, kSize size, bool bigEndian = true) override;

    /**
     * Returns size of serialized GoDataTree object.
     */
    kSize Count();

private:
    kSize count;
};

/*! @class GoDataTreeSerializerOutputStreamWriter GoDataTreeSerializerOutputWriter.h

@brief A class used to write bytes into kStream.
@ingroup Go
*/
class GoApiClass GoDataTreeSerializerOutputStreamWriter : public IGoDataTreeSerializerOutputWriter
{
public:
    /**
     * Constructs the GoDataTreeSerializerOutputStreamWriter object.
     *
     * @param output                            kStream output.
     */
    explicit GoDataTreeSerializerOutputStreamWriter(kStream output);

    /* IGoDataTreeSerializerOutputWriter overrides */
    void WriteByte(kByte byte) override;
    void WriteByteArray(kByte* bytes, kSize size) override;
    void WriteByteArray(StreamFx streamingFx) override;
    void WriteTextArray(kByte* text, kSize size) override;
    void WriteNumberBytes(kByte* bytes, kSize size, bool bigEndian = true) override;

private:
    kStream output;
};

/*! @class GoDataTreeSerializerOutputWriter GoDataTreeSerializerOutputWriter.h

@brief A class used to write bytes into kSerializer.
@ingroup Go
*/
class GoApiClass GoDataTreeSerializerOutputWriter : public IGoDataTreeSerializerOutputWriter
{
public:
    /**
     * Constructs the GoDataTreeSerializerOutputWriter object.
     *
     * @param output                            kSerializer output.
     */
    explicit GoDataTreeSerializerOutputWriter(kSerializer output);

    /* IGoDataTreeSerializerOutputWriter overrides */
    void WriteByte(kByte byte) override;
    void WriteByteArray(kByte* bytes, kSize size) override;
    void WriteByteArray(StreamFx streamingFx) override;
    void WriteTextArray(kByte* text, kSize size) override;
    void WriteNumberBytes(kByte* bytes, kSize size, bool bigEndian = true) override;

// Member variables
private:
    kSerializer output;
};

template<typename ContainerT>
inline GoDataTreeSerializerOutputContainerWriter<ContainerT>::GoDataTreeSerializerOutputContainerWriter(ContainerT* output) :
    output(output)
{ }

template<typename ContainerT>
inline void GoDataTreeSerializerOutputContainerWriter<ContainerT>::WriteByte(kByte byte)
{
    output->push_back(byte);
}

template<typename ContainerT>
inline void GoDataTreeSerializerOutputContainerWriter<ContainerT>::WriteByteArray(kByte* bytes, kSize size)
{
    output->insert(output->end(), &bytes[0], &bytes[size]);
}

template<typename ContainerT>
inline void GoDataTreeSerializerOutputContainerWriter<ContainerT>::WriteByteArray(StreamFx streamingFx)
{

}

template<typename ContainerT>
inline void GoDataTreeSerializerOutputContainerWriter<ContainerT>::WriteTextArray(kByte* text, kSize size)
{
    output->insert(output->end(), &text[0], &text[size]);
}

template<typename ContainerT>
inline void GoDataTreeSerializerOutputContainerWriter<ContainerT>::WriteNumberBytes(kByte* bytes, kSize size, bool bigEndian)
{
    if (bigEndian)
    {
        std::reverse_copy(&bytes[0], &bytes[size], std::back_inserter(*output));
    }
    else
    {
        std::copy(&bytes[0], &bytes[size], std::back_inserter(*output));
    }
}

} // namespace

#endif // GOAPI_GODATATREE_SERIALIZER_GODATATREESERIALIZEROUTPUTWRITER_H
