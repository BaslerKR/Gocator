/**\file    ReqRespInfo.h
 */

#ifndef GOAPI_REQ_RESP_INFO_H
#define GOAPI_REQ_RESP_INFO_H

#include <GoApi/GoApiDef.h>
#include <GoApi/GoDataTree/GoDataTree.h>

#include <map>

using GoDataTree = GoApi::GoDataTree;

namespace GoApi
{

using ResourceKeys = std::map<std::string, std::string>;

class GoApiClass RequestInfo
{
public:
    /**
     * Resource keys used to get collection ids.
     */
    ResourceKeys keys;

    /**
     * Optional GoDataTree arguments used in the request.
     */
    GoDataTree args;

    /**
     * Boolean describing if schema should be included.
     * Only used for Read.
     */
    bool includeSchema;

    /**
     * RequestInfo constructor for an empty request.
     */
    RequestInfo();

    /**
     * RequestInfo constructor with keys.
     *
     * @param keys              ResourceKeys.
     */
    RequestInfo(const ResourceKeys& keys);

    /**
     * RequestInfo constructor with keys and GoDataTree args.
     *
     * @param keys              ResourceKeys.
     * @param args              GoDataTree arguments to be used.
     */
    RequestInfo(const ResourceKeys& keys, const GoDataTree& args);

    /**
     * RequestInfo constructor with keys and GoDataTree args and includeSchema flag for reading.
     *
     * @param keys              ResourceKeys.
     * @param args              GoDataTree arguments to be used.
     * @param includeSchema     includeSchema flag.
     */
    RequestInfo(const ResourceKeys& keys, const GoDataTree& args, bool includeSchema);

    /**
     * Returns the value of a key.
     *
     * @param   id      Id of the key.
     *
     * @return          Pointer to the value. nullptr if key is not found.
     */
    const std::string* FindKey(const std::string& id) const;
};

class GoApiClass ResponseInfo
{
public:

    /**
    * Representations a path reference.
    */
    struct PathRef
    {
        std::string path;       /*!< The path as a string. */
        GoDataTree extra;       /*!< Annotations for links, or extra properties for embedded items. */
    };

    /**
    * Representations a link relation as a collection.
    */
    struct RefCollection
    {
        bool forceArray = false;        /*!< Whether or not to force the relation as an array. */
        bool forceExpansion = false;    /*!< Whether or not the relation should always be expanded. */
        std::vector<PathRef> refs;      /*!< The list of references. */
    };

    using RefPtrMap = std::map<std::string, const RefCollection*>;

    /**
    * Add a link.
    *
    * Links are added to the "_links" property of the GoDataTree representation, based on the HAL standard.
    *
    *
    * @param rel            The relation type of the link. If not using standard IANA types like "item", prefix with "go:", e.g. "go:toolInput".
    * @param path           The path of the link.
    * @param forceArray     Force the link relation to be an array. See @ref ForceLinkArray.
    * @param annotations    Optional annotations for the link. Properties are added under the relation object, i.e. next to "href".
    */
    void Link(const std::string& rel, const std::string& path, bool forceArray = false, const GoDataTree& annotations = {});

    /**
    * Add an embedded resource as link.
    *
    * Embedded resources are added to the "_embedded" property of the GoDataTree representation, based on the HAL standard.
    * The Read method can automatically expand embedded links recursively on demand.
    *
    * See @ref Link for more information on the HAL standard and examples.
    *
    * @param rel            The relation type of the link. Same as @ref Link.
    * @param path           The path of the link.
    * @param forceArray     Force the link relation to be an array. See @ref ForceEmbeddedArray.
    * @param extraProps     Optional properties to add to the embedded resource.
    */
    void Embed(const std::string& rel, const std::string& path, bool forceArray = false, const GoDataTree& extraProps = {});

    /**
    * Force a link relation to be an array.
    *
    * When a link relation is forced to be an array, the resulting GoDataTree is an array, even if there is only one item
    * In fact, an empty array is created even if no item has been added.
    *
    * @param rel        The relation type to make as array.
    */
    void ForceLinkArray(const std::string& rel);

    /**
    * Force an embedded relation to be an array.
    *
    * See @ref ForceLinkArray for more information.
    *
    * @param rel        The relation type to make as array.
    */
    void ForceEmbeddedArray(const std::string& rel);

    /**
    * Force an embedded relation to always be expanded.
    *
    * By default, an embedded resource contains only a link, and is only expanded
    * when requested by the user. This function overrides that behavior and ensures
    * the embedded resources are always expanded.
    *
    * @param rel        The relation type to expand.
    */
    void ForceEmbeddedExpansion(const std::string& rel);

    /**
    * Retrieve all added links.
    *
    * @return       A map of rel -> reference items.
    */
    RefPtrMap GetLinks() const;

    /**
    * Retrieve all added embedded items.
    *
    * @return       A map of rel -> reference items.
    */
    RefPtrMap GetEmbeddings() const;

private:
    using RefMap = std::map<std::string, RefCollection>;

    RefMap links;
    RefMap embeddings;

    void AddRef(RefMap& refMap, const std::string& rel, const std::string& path, const GoDataTree& extra);

    RefPtrMap GetRefPtrMap(const RefMap& refMap) const;
};

} // Namespace

#endif  // GOAPI_REQ_RESP_INFO_H
