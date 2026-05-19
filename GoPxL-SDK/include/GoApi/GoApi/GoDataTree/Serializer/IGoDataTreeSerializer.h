/**@file    IGoDataTreeSerializer.h
 * Defines the IGoDataTreeSerializer class.
 */

#ifndef GOAPI_GODATATREE_SERIALIZER_IGODATATREESERIALIZER_H
#define GOAPI_GODATATREE_SERIALIZER_IGODATATREESERIALIZER_H

#include <GoApi/GoApiDef.h>
#include <functional>

namespace GoApi
{

using StreamFx = std::function<void()>;
using BinSizeFx = std::function<k64u()>;

template <typename T>
class GoApiClass IGoDataTreeSerializer
{
public:
    /**
     * Serializes GoDataTree into T.
     *
     * @param tree                              GoDataTree input.
     * @param out                               Output T.
     */
    virtual void Serialize(const GoDataTree& tree, T& out) = 0;

    /**
     * Serializes GoDataTree into kStream.
     *
     * @param tree                              GoDataTree input.
     * @param out                               Output stream or kSerializer to write into.
     */
    virtual void Serialize(const GoDataTree& tree, kObject out) = 0;

    /**
     * Serializes GoDataTree into kStream handling binary data via given function instead.
     * 
     * This function should be used if binary data should be streamed during serialization instead of
     * serializaing the binary data from memory.
     *
     * @param source                        GoDataTree input.
     * @param out                           Output stream.
     * @param binarySize                    Pre-calculated size of binary to be streamed in bytes.
     * @param streamingFx                   Function to be executed to stream binary data.
     */
    virtual void Serialize(const GoDataTree& source, kObject out, kSize binarySize, StreamFx streamingFx) = 0;

    /**
     * Deserializes T into GoDataTree.
     *
     * @param input                             Input T.
     * @param out                               Output tree.
     */
    virtual void Deserialize(const T& input, GoDataTree& out) = 0;

    /**
     * Deserializes kStream into GoDataTree.
     *
     * @param input                             Input stream.
     * @param size                              Input size.
     * @param out                               Output tree.
     */
    virtual void Deserialize(kStream input, kSize size, GoDataTree& out) = 0;

    /**
     * Calculates serialized output size in bytes. 
     * 
     * @param input                             Input tree.
     * @return                                  Output size.
     */
    virtual kSize SerializedOutputSize(const GoDataTree& input) = 0;

    /**
     * Calculates serialized output size in bytes along with specifying binary size
     * to support streamed data whose binary size has to be pre-calculated.
     * 
     * This function should be called when the binary size of the input cannot be determined
     * due to not being in memory to calculate from, this takes in the pre-calculated binary size
     * as an input so it can calculate the total output size with that without having the GoDataTree
     * object to be in memory.
     * 
     * The binary size input is the size of the binary contained in the GoDataTree input,
     * the return value will be the sum of the binary size + the rest of the GoDataTree object.
     *
     * @param input                             Input tree.
     * @param binarySize                        Pre-calculated size of binary to be streamed in bytes.
     * @return                                  Output size.
     */
    virtual kSize SerializedOutputSize(const GoDataTree& input, kSize binarySize) = 0;
};

} // namespace

#endif // GOAPI_GODATATREE_SERIALIZER_IGODATATREESERIALIZER_H
