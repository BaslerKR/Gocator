/**
 * @file    GoResource.h
 * @brief   Declares the GoPxLSdk.GoResource and GoPxLSdk.GoUpdateScope classes.
 *
 * @internal
 * Copyright (C) 2024-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_RESOURCE_H
#define GO_PXL_SDK_RESOURCE_H

#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoTransaction.h>
#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace GoPxLSdk
{

class GoRestClient;
class GoResourceManager;
class GoUpdateScope;

/**
 * Predefined HAL relation types for child resource enumeration.
 *
 * Use these constants with GoResource::ChildUris(), Children(), and ChildCount()
 * for well-known relation types. Custom relation type strings can also be passed
 * directly to these methods.
 *
 * @version               Introduced in 1.5.0.0
 */
struct GoPxLSdkClass GoRelationType
{
    /** Generic collection children (e.g., tools in /tools, engines in /scan/engines). */
    static const std::string Item;

    /** Scanners embedded under a scan engine (e.g., /scan/engines/{id}). */
    static const std::string Scanner;

    /** Sub-tasks embedded under a task (e.g., /tasks/{id}). */
    static const std::string SubTask;

    /** Content resources embedded under a parent. */
    static const std::string Content;

    /** Commands linked under a resource (e.g., start, stop). */
    static const std::string Command;

    /** Actions linked under a resource (e.g., tool actions). */
    static const std::string Action;
};

/**
 * Provides high-level access to a single REST resource on a Gocator sensor.
 *
 * GoResource encapsulates a resource URI and a GoRestClient reference, offering
 * typed getters/setters, caching, deferred writes, and schema access. It eliminates
 * the boilerplate of constructing transactions, waiting for responses, and navigating
 * JSON payloads that raw GoRestClient usage requires.
 *
 * GoResource is not thread-safe. Each thread should use its own GoResource instance.
 * Multiple GoResource instances sharing one GoRestClient is safe (GoRestClient has
 * its own mutex).
 *
 * Concurrency: Updates use JSON Merge Patch (RFC 7396) with last-writer-wins
 * semantics. If two clients (e.g., an SDK program and the GoPxL GUI) modify the
 * same property concurrently, the last write to reach the server takes effect with
 * no error reported to either client. This applies to both immediate writes (SetXxx)
 * and deferred writes (BeginUpdate/EndUpdate). The Gocator REST protocol does not
 * support conditional updates (e.g., ETags or version numbers), so optimistic
 * concurrency control is not available. If your application needs to detect external
 * changes before writing, subscribe to the resource and check HasRemoteChanges()
 * before calling EndUpdate — but note that notifications are asynchronous and may
 * not have arrived yet at the time of the check.
 *
 * Lifetime: GoResource stores a raw pointer to the GoRestClient. The client must
 * outlive all GoResource instances (including shared_ptr holders from the manager).
 * Destroying the GoRestClient while GoResource instances exist is undefined behavior.
 *
 * @version               Introduced in 1.5.0.0
 */
class GoPxLSdkClass GoResource
{
public:
    static constexpr int DEFAULT_TIMEOUT_MS = 5000;
    static constexpr int DEFAULT_EXPAND_LEVEL = 0;

    /**
     * Constructs a GoResource bound to a URI on the given client.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param client                        The REST client to use for server communication.
     * @param uri                           The resource URI (e.g., "/system").
     */
    GoResource(GoRestClient& client, const std::string& uri);

    ~GoResource();

    GoResource(const GoResource& other);
    GoResource& operator=(const GoResource& other);
    GoResource(GoResource&& other) noexcept;
    GoResource& operator=(GoResource&& other) noexcept;

    // -- URI -----------------------------------------------------------

    /**
     * Gets the URI this resource is bound to.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The resource URI string.
     */
    const std::string& Uri() const;

    // -- Configuration -------------------------------------------------

    /**
     * Sets the timeout for server operations in milliseconds.
     * Default: DEFAULT_TIMEOUT_MS (5000).
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param timeoutMs                     Timeout in milliseconds.
     */
    void SetTimeout(int timeoutMs);

    /**
     * Gets the current timeout for server operations in milliseconds.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              Timeout in milliseconds.
     */
    int Timeout() const;

    /**
     * Sets the expand level used when reading resources from the server.
     * Controls how deeply embedded resources are expanded in the response.
     * Default: DEFAULT_EXPAND_LEVEL (0, no expansion).
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param level                         The expand level (0 = no expansion).
     */
    void SetExpandLevel(int level);

    /**
     * Gets the current expand level.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The expand level.
     */
    int ExpandLevel() const;

    /**
     * Enables or disables automatic schema fetching with Cache().
     * When enabled, Cache() includes the schema in the read request and caches it.
     * Default: false.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param enabled                       True to fetch schema with Cache().
     */
    void SetAutoFetchSchema(bool enabled);

    /**
     * Gets whether automatic schema fetching is enabled.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if auto-fetch schema is enabled.
     */
    bool AutoFetchSchema() const;

    /**
     * Enables or disables automatic cache invalidation after writes.
     * When enabled, successful Set/Update/Flush operations invalidate the cache.
     * Default: true.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param enabled                       True to auto-invalidate on writes.
     */
    void SetAutoInvalidation(bool enabled);

    /**
     * Gets whether automatic cache invalidation is enabled.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if auto-invalidation is enabled.
     */
    bool AutoInvalidation() const;

    // -- Subscription & Remote Changes ---------------------------------

    /**
     * Subscribes this resource to server notifications via the GoRestClient
     * notification listener API. When a notification matching this resource's
     * URI arrives, the hasRemoteChanges flag is set, causing the next getter
     * to re-read from the server.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoChannelError     If the subscription request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void Subscribe();

    /**
     * Unsubscribes this resource from server notifications.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     */
    void Unsubscribe();

    /**
     * Checks whether this resource is subscribed to server notifications.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if subscribed.
     */
    bool IsSubscribed() const;

    /**
     * Checks whether remote changes have been detected since the last read.
     * When true, the next getter call will re-read from the server.
     * The flag is set by the GoResourceManager (for managed resources)
     * or by subscription notification callbacks.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if remote changes are pending.
     */
    bool HasRemoteChanges() const;

    /**
     * Checks whether this resource is managed by a GoResourceManager.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if managed.
     */
    bool IsManaged() const;

    /**
     * Checks whether this resource has been marked as deleted.
     *
     * A resource is marked as deleted when:
     * - Delete() is called successfully on it
     * - A server operation returns kERROR_NOT_FOUND (-999)
     * - A "deleted" notification is received for this resource's URI
     *
     * Once deleted, most operations throw GoResourceError immediately
     * without making a network request.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if the resource is deleted.
     */
    bool IsDeleted() const;

    // -- Cache Operations ----------------------------------------------

    /**
     * Reads the resource from the server and caches it locally.
     * Subsequent getter calls use the cached data until the cache is invalidated.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void Cache();

    /**
     * Invalidates the local data and schema caches, forcing the next getter
     * call to re-read from the server.
     *
     * Most users do not need to call this method. Managed resources (created
     * via GoSystem::Resource()) are automatically invalidated in two ways:
     *
     * - **After any mutating operation** (Set, Flush, CreateChild, Delete,
     *   Call, CallCommand, CallAction): the GoResourceManager marks all
     *   unsubscribed managed resources as having remote changes, so the
     *   next read re-fetches from the server automatically.
     *
     * - **Via subscriptions**: subscribed resources receive server-side
     *   change notifications that trigger automatic re-reads.
     *
     * InvalidateCache is primarily useful for **unmanaged** resources
     * (constructed directly with `GoResource(client, uri)` rather than
     * through `GoSystem::Resource()`) where automatic invalidation does
     * not apply. It may also be needed when external changes are expected
     * but undetected — for example, when a separate client or application
     * modifies the sensor state outside of this SDK session.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     */
    void InvalidateCache();

    /**
     * Returns the full cached resource data as a GoJson object.
     * If the cache is empty or remote changes have been detected, reads from
     * the server first (lazy caching). Subsequent calls return the cached copy
     * without a network round-trip until the cache is invalidated.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The full resource payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson Data();

    // -- Typed Getters -------------------------------------------------

    /**
     * Gets a boolean property. Performs a lazy read if the cache is empty.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path (e.g., "/autostart" or "autostart").
     * @return                              The boolean value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    bool GetBool(const std::string& path);

    /**
     * Gets an integer property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The integer value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    int GetInt(const std::string& path);

    /**
     * Gets a 64-bit integer property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The 64-bit integer value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    int64_t GetInt64(const std::string& path);

    /**
     * Gets a float property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The float value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    float GetFloat(const std::string& path);

    /**
     * Gets a double property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The double value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    double GetDouble(const std::string& path);

    /**
     * Gets a string property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The string value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist or type mismatch.
     */
    std::string GetString(const std::string& path);

    /**
     * Gets a sub-object or array as a GoJson. Use for nested structures.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The GoJson sub-object.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist.
     */
    GoJson GetObject(const std::string& path);

    /**
     * Gets an arbitrary property as a GoJson value. Works for any JSON type.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @return                              The GoJson value.
     * @throws GoPxLSdk::GoResourceError    If the path does not exist.
     */
    GoJson GetProp(const std::string& path);

    // -- Typed Setters -------------------------------------------------

    /**
     * Sets a boolean property. If inside a BeginUpdate/EndUpdate block,
     * the change is deferred until EndUpdate; otherwise it is sent immediately.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     * @throws GoPxLSdk::GoChannelError     If the request times out (immediate mode).
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error (immediate mode).
     */
    void SetBool(const std::string& path, bool value);

    /**
     * Sets an integer property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    void SetInt(const std::string& path, int value);

    /**
     * Sets a 64-bit integer property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    void SetInt64(const std::string& path, int64_t value);

    /**
     * Sets a float property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    void SetFloat(const std::string& path, float value);

    /**
     * Sets a double property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    void SetDouble(const std::string& path, double value);

    /**
     * Sets a string property.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    void SetString(const std::string& path, const std::string& value);

    /**
     * Sets a property of any supported type via template.
     * Supported types: int, bool, float, double, int64_t, std::string.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to set.
     */
    template <typename T>
    void SetProp(const std::string& path, const T& value);

    /**
     * Applies a JSON Merge Patch (RFC 7396) to this resource.
     * If inside a BeginUpdate/EndUpdate block, the patch is merged into the
     * pending changes and sent when the outermost block ends. Otherwise the
     * patch is sent to the server immediately.
     *
     * This is the GoJson equivalent of the typed setters. Use it when you
     * already have a GoJson payload or prefer to specify multiple properties
     * as a JSON object rather than individual Set calls.
     *
     * @code
     * // These two approaches are equivalent:
     * resource->SetJson(GoJson(R"({"parameters": {"exposure": 1200}})"));
     *
     * resource->SetInt("/parameters/exposure", 1200);
     * @endcode
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param patch                         A JSON Merge Patch payload.
     * @throws GoPxLSdk::GoChannelError     If the request times out (immediate mode).
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error (immediate mode).
     */
    void SetJson(const GoJson& patch);

    // -- Deferred Writes -----------------------------------------------

    /**
     * Begins a deferred update block. Subsequent Set calls are batched and
     * not sent until EndUpdate() is called. Supports nesting via reference counting.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     */
    void BeginUpdate();

    /**
     * Ends a deferred update block. When the outermost block ends, the
     * accumulated changes are flushed to the server as a single update.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoResourceError    If called without a matching BeginUpdate().
     * @throws GoPxLSdk::GoChannelError     If the flush request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void EndUpdate();

    /**
     * Returns an RAII scope guard that calls BeginUpdate() on construction and
     * EndUpdate() on destruction. On exception unwinding, pending changes are
     * discarded instead of flushed.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              A GoUpdateScope bound to this resource.
     */
    GoUpdateScope ScopedUpdate();

    /**
     * Cancels the current deferred update, discarding all pending changes.
     * The deferred depth is not modified — callers are still expected to call
     * EndUpdate() (or let GoUpdateScope destructor run) to maintain proper
     * Begin/End pairing. When the outermost EndUpdate decrements depth to zero,
     * the cancelled flag is cleared and no flush is performed.
     *
     * This allows intermediate code to signal "abort this batch" without
     * disrupting the call stack of nested Begin/End pairs.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     */
    void CancelUpdate();

    /**
     * Returns true if CancelUpdate() has been called during the current
     * deferred update block. Callers can check this to skip subsequent
     * SetXxx() calls that would be discarded anyway.
     *
     * The flag is cleared when the outermost EndUpdate() decrements the
     * deferred depth to zero.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if the current update has been cancelled.
     */
    bool IsUpdateCancelled() const;

    /**
     * Checks whether an update is currently deferred (inside a BeginUpdate/EndUpdate block).
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if updates are being deferred.
     */
    bool IsUpdateDeferred() const;

    /**
     * Flushes any pending deferred changes to the server immediately.
     * If there are no pending changes, this is a no-op.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void Flush();

    /**
     * Discards all pending deferred changes without sending them.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     */
    void DiscardPending();

    // -- Schema & HAL --------------------------------------------------

    /**
     * Gets the full schema for this resource. If the schema is not already cached,
     * performs a Read with includeSchema=true to fetch it. The data returned by this
     * read is also cached opportunistically.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The schema as a GoJson object.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson Schema();

    /**
     * Gets the sub-schema for a property at the given path.
     * Performs type-aware traversal through objects and arrays.
     * Fetches the schema from the server if not already cached.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path (e.g., "/runState" or "runState").
     * @return                              The sub-schema for the property.
     * @throws GoPxLSdk::GoResourceError    If the path cannot be resolved in the schema.
     * @throws GoPxLSdk::GoChannelError     If the schema fetch times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson SchemaFor(const std::string& path);

    /**
     * Enables or disables client-side schema validation on setters.
     * When enabled, every Set call validates the value against the schema
     * before sending it to the server. Invalid values throw GoResourceValidationError.
     * Default: false.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param enable                        True to enable validation.
     */
    void EnableValidation(bool enable = true);

    /**
     * Gets whether client-side schema validation is enabled.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              True if validation is enabled.
     */
    bool IsValidationEnabled() const;

    /**
     * Validates a value against the schema for the given property path.
     * Does not send anything to the server. Useful for pre-flight checks.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param path                          JSON pointer path.
     * @param value                         The value to validate.
     * @param errors  [out]                 Optional. Receives error descriptions.
     * @return                              True if validation passes.
     * @throws GoPxLSdk::GoResourceError    If the path cannot be resolved in the schema.
     */
    bool Validate(const std::string& path, const GoJson& value,
                  std::vector<std::string>* errors = nullptr);

    /**
     * Gets the _links section from the cached data.
     * Returns an empty GoJson if _links is not present.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The _links object.
     */
    GoJson Links();

    /**
     * Gets the _embedded section from the cached data.
     * Returns an empty GoJson if _embedded is not present.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The _embedded object.
     */
    GoJson Embedded();

    // -- Collection Enumeration ---------------------------------------

    /**
     * The default HAL relation type used for child enumeration.
     * Corresponds to the generic "item" relation in HAL.
     */
    static const std::string DEFAULT_RELATION_TYPE;

    /**
     * Gets the URIs of child resources using the default "item" relation type.
     * Reads from the cached _embedded metadata; triggers a lazy Cache() if needed.
     * Returns an empty vector if the resource has no _embedded section.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              URIs of child resources.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::string> ChildUris();

    /**
     * Gets the URIs of child resources for a specific HAL relation type.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param relationType                  The HAL relation type (e.g., "item", "go:scanner").
     * @return                              URIs of child resources for the given relation type.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::string> ChildUris(const std::string& relationType);

    /**
     * Gets shared pointers for all children using the default "item" relation type.
     *
     * Routes children through the GoResourceManager (single instance per URI).
     * Requires a manager; throws GoResourceError if unmanaged. If children have
     * expanded data in the parent's response, their caches are pre-seeded.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              Shared pointers to child GoResource instances.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::shared_ptr<GoResource>> Children();

    /**
     * Gets shared pointers for all children with a specific HAL relation type.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param relationType                  The HAL relation type (e.g., "item", "go:scanner").
     * @return                              Shared pointers to child GoResource instances.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::shared_ptr<GoResource>> Children(const std::string& relationType);

    /**
     * Gets the number of children using the default "item" relation type.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              Count of children.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    size_t ChildCount();

    /**
     * Gets the number of children for a specific HAL relation type.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param relationType                  The HAL relation type.
     * @return                              Count of children.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    size_t ChildCount(const std::string& relationType);

    // -- Child Access -------------------------------------------------

    /**
     * Returns a child GoResource by appending the given path to this resource's URI.
     * This is purely client-side path construction; no server request is made.
     *
     * Routes the child through the GoResourceManager (single instance per URI).
     * Requires a manager; throws GoResourceError if unmanaged.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param childPath                     The child's ID or relative path (e.g., "scanner-0", "commands/start").
     * @return                              Shared pointer to the child GoResource.
     */
    std::shared_ptr<GoResource> Child(const std::string& childPath);

    // -- Collection Mutations -----------------------------------------

    /**
     * Creates a new child resource on the server and returns a shared pointer
     * to the managed GoResource bound to the new child's URI.
     * The URI is extracted from the create response's path field; if absent, falls
     * back to _links.self.href in the response payload.
     * Invalidates the cache after creation. Requires a GoResourceManager.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param arguments                     Creation arguments (server-defined schema).
     * @return                              Shared pointer to the newly created child GoResource.
     * @throws GoPxLSdk::GoResourceError    If no manager, or server response contains no usable URI.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::shared_ptr<GoResource> CreateChild(const GoJson& arguments);

    /**
     * Deletes this resource from the server.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void Delete();

    /**
     * Deletes the child resource at the given URI.
     * Invalidates this resource's cache after deletion.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param childUri                      The full URI of the child resource to delete.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void DeleteChild(const std::string& childUri);

    /**
     * Deletes all children using the default "item" relation type.
     * Enumerates ChildUris() and deletes each child.
     * Invalidates the cache after all deletions.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @throws GoPxLSdk::GoChannelError     If a request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void DeleteAllChildren();

    /**
     * Deletes all children for a specific HAL relation type.
     * Enumerates ChildUris(relationType) and deletes each child.
     * Invalidates the cache after all deletions.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param relationType                  The HAL relation type (e.g., "go:scanner").
     * @throws GoPxLSdk::GoChannelError     If a request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void DeleteAllChildren(const std::string& relationType);

    // -- Callable Resource Support -------------------------------------

    /**
     * Calls this resource directly (the resource itself is callable).
     * Delegates to GoRestClient::Call() and returns the response payload.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson Call(int timeoutMs = 0);

    /**
     * Calls this resource with arguments.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param arguments                     Arguments to pass to the callable resource.
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson Call(const GoJson& arguments, int timeoutMs = 0);

    // -- Command/Action Convenience ------------------------------------

    /**
     * Calls a named command under this resource.
     * Constructs {uri}/commands/{commandName} and calls it.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param commandName                   The command name (e.g., "start", "stop").
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson CallCommand(const std::string& commandName, int timeoutMs = 0);

    /**
     * Calls a named command with arguments.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param commandName                   The command name.
     * @param arguments                     Arguments to pass to the command.
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson CallCommand(const std::string& commandName, const GoJson& arguments, int timeoutMs = 0);

    /**
     * Calls a named action under this resource.
     * Constructs {uri}/actions/{actionName} and calls it.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param actionName                    The action name (e.g., "Info", "Warn").
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson CallAction(const std::string& actionName, int timeoutMs = 0);

    /**
     * Calls a named action with arguments.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param actionName                    The action name.
     * @param arguments                     Arguments to pass to the action.
     * @param timeoutMs                     Timeout in milliseconds. 0 uses the resource default.
     * @return                              The response payload as GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson CallAction(const std::string& actionName, const GoJson& arguments, int timeoutMs = 0);

    // -- Command/Action Enumeration ------------------------------------

    /**
     * Lists command names available on this resource.
     * Extracts names from _links["go:command"] by taking the last path segment
     * of each href. Triggers a lazy Cache() if data is not cached.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              Command names (e.g., {"start", "stop"}).
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::string> CommandNames();

    /**
     * Lists action names available on this resource.
     * Extracts names from _links["go:action"] by taking the last path segment
     * of each href. Triggers a lazy Cache() if data is not cached.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              Action names (e.g., {"Info", "Warn", "Error"}).
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    std::vector<std::string> ActionNames();

    // -- Callable Arguments --------------------------------------------

    /**
     * Reads this callable resource and returns the "parameters" property.
     * Provides default argument values that can be modified and passed to Call().
     * Returns an empty GoJson if no "parameters" property exists.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The parameters sub-object, or empty GoJson.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson CallArguments();

    // -- Low-Level Escape Hatches --------------------------------------

    /**
     * Reads the resource from the server. The response payload and any _schema
     * present are cached automatically.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @return                              The response payload.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    GoJson Read();

    /**
     * Reads the resource from the server with custom arguments. The response
     * payload and any _schema present are cached automatically.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param args                          GoJson arguments for the read request.
     * @return                              The response payload.
     */
    GoJson Read(const GoJson& args);

    /**
     * Performs a raw update, bypassing the deferred model.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param payload                       The JSON Merge Patch payload.
     * @throws GoPxLSdk::GoChannelError     If the request times out.
     * @throws GoPxLSdk::GoRequestError     If the server responds with an error.
     */
    void Update(const GoJson& payload);

    /**
     * Performs a raw update with custom arguments.
     *
     * @public                              @memberof GoResource
     * @version                             Introduced in 1.5.0.0
     * @param payload                       The JSON Merge Patch payload.
     * @param args                          GoJson arguments for the update request.
     */
    void Update(const GoJson& payload, const GoJson& args);

private:
    friend class GoUpdateScope;
    friend class GoResourceManager;

    GoResource(GoRestClient& client, const std::string& uri, GoResourceManager* manager);

    const GoJson& EnsureData();
    void ApplySet(const GoJson& patchPayload);
    void CachePayload(const GoJson& payload);
    std::vector<std::string> LinkNames(const std::string& relationType);
    void EnsureSchema();
    void ValidateIfEnabled(const std::string& path, const GoJson& value);
    void CheckDeleted() const;
    void MarkDeleted();
    void ReregisterNotificationListener();
    static std::string NormalizePath(const std::string& path);
    static bool IsExpanded(const GoJson& itemData);

    GoRestClient* client_ = nullptr;
    std::string uri_;
    GoResourceManager* manager_ = nullptr;

    int timeoutMs_ = DEFAULT_TIMEOUT_MS;
    int expandLevel_ = DEFAULT_EXPAND_LEVEL;
    bool autoFetchSchema_ = false;
    bool autoInvalidation_ = true;

    GoJson cachedData_;
    bool dataCacheValid_ = false;

    GoJson cachedSchema_;
    bool schemaCacheValid_ = false;
    bool validationEnabled_ = false;

    GoJson pendingPatch_;
    int deferredDepth_ = 0;
    bool cancelled_ = false;

    std::atomic<bool> hasRemoteChanges_{false};
    std::atomic<bool> deleted_{false};
    std::atomic<bool> subscribed_{false};
    size_t notificationListenerId_ = 0;
};


/**
 * RAII scope guard for GoResource deferred updates.
 *
 * Calls BeginUpdate() on construction. On normal destruction, calls EndUpdate()
 * which flushes pending changes. On exception unwinding (detected via
 * std::uncaught_exceptions()), pending changes are discarded to prevent
 * half-configured patches from reaching the sensor.
 *
 * @version               Introduced in 1.5.0.0
 */
class GoPxLSdkClass GoUpdateScope
{
public:
    /**
     * Constructs a GoUpdateScope and begins a deferred update on the given resource.
     *
     * @public                              @memberof GoUpdateScope
     * @version                             Introduced in 1.5.0.0
     * @param resource                      The resource to manage.
     */
    explicit GoUpdateScope(GoResource& resource);

    /**
     * Destroys the GoUpdateScope. Flushes on normal exit, discards on exception.
     *
     * @public                              @memberof GoUpdateScope
     * @version                             Introduced in 1.5.0.0
     */
    ~GoUpdateScope();

    /**
     * Cancels the update, discarding all pending changes. The scope guard
     * destructor will still call EndUpdate() to maintain proper depth pairing,
     * but no flush will occur because the cancelled flag is set.
     *
     * @public                              @memberof GoUpdateScope
     * @version                             Introduced in 1.5.0.0
     */
    void Cancel();

    GoUpdateScope(const GoUpdateScope&) = delete;
    GoUpdateScope& operator=(const GoUpdateScope&) = delete;

    GoUpdateScope(GoUpdateScope&& other) noexcept;
    GoUpdateScope& operator=(GoUpdateScope&& other) noexcept;

private:
    GoResource* resource_;
    int uncaughtOnConstruction_;
};


// -- Template Implementation -----------------------------------------------

template <typename T>
void GoResource::SetProp(const std::string& path, const T& value)
{
    std::string normalizedPath = NormalizePath(path);
    GoJson patchPayload;
    patchPayload.Set<T>(normalizedPath, value);
    ValidateIfEnabled(normalizedPath, patchPayload.At(normalizedPath));
    ApplySet(patchPayload);
}

}

#endif
