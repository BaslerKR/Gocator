/**
 * @file    GoResourceManager.h
 * @brief   Declares the GoPxLSdk.GoResourceManager class.
 *
 * @internal
 * Copyright (C) 2024-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GORESOURCEMANAGER_H
#define GO_PXL_SDK_GORESOURCEMANAGER_H

#include <GoPxLSdk/Def.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace GoPxLSdk
{

class GoRestClient;
class GoResource;

/**
 * Manages a URI-keyed map of GoResource instances for a GoRestClient.
 *
 * Provides single instance per URI (eliminates duplicate caches), coarse-grained
 * cache invalidation on non-idempotent requests, and shared ownership of managed
 * GoResource instances. When subscription-optimized invalidation is enabled
 * (the default), subscribed resources are skipped during coarse invalidation
 * since they receive fine-grained notification updates instead.
 *
 * GoResourceManager does NOT handle notification routing — that is handled
 * by GoRestClient's notification listener map, which individual GoResource
 * instances register with directly via Subscribe().
 *
 * Thread-safe: mutex protects internal map; invalidation only sets atomic
 * flags on managed resources.
 *
 * @version               Introduced in 1.5.0.0
 */
class GoPxLSdkClass GoResourceManager
{
public:
    explicit GoResourceManager(GoRestClient& client);
    ~GoResourceManager();

    // Non-copyable, non-movable
    GoResourceManager(const GoResourceManager&) = delete;
    GoResourceManager& operator=(const GoResourceManager&) = delete;

    /**
     * Returns the managed resource for the given URI.
     *
     * Creates a new GoResource if one does not exist in the map. The caller
     * receives shared ownership via std::shared_ptr. If autoSubscribe is
     * enabled, newly created resources are automatically subscribed to
     * server notifications.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @param uri                           The resource URI (e.g., "/system").
     * @return                              Shared pointer to the managed GoResource.
     */
    std::shared_ptr<GoResource> GetOrCreate(const std::string& uri);

    /**
     * Enables or disables automatic subscription for newly created resources.
     *
     * When enabled, resources created via GetOrCreate() are automatically
     * subscribed to server notifications. Already-existing unsubscribed resources
     * in the map are NOT retroactively subscribed. Default: false.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @param enabled                       True to auto-subscribe new resources.
     */
    void SetAutoSubscribe(bool enabled);

    /**
     * Returns whether auto-subscribe is enabled.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @return                              True if auto-subscribe is enabled.
     */
    bool AutoSubscribe() const;

    /**
     * Enables or disables automatic client-side schema validation for newly
     * created resources.
     *
     * When enabled, resources created via GetOrCreate() automatically have
     * EnableValidation(true) called on them. Already-existing resources in the
     * map are NOT retroactively affected. Default: false.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @param enabled                       True to auto-enable validation on new resources.
     */
    void SetAutoValidation(bool enabled);

    /**
     * Returns whether auto-validation is enabled.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @return                              True if auto-validation is enabled.
     */
    bool AutoValidation() const;

    /**
     * Subscribes all currently managed resources that are not already subscribed.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void SubscribeAll();

    /**
     * Unsubscribes all currently managed resources that are subscribed.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void UnsubscribeAll();

    /**
     * Enables client-side schema validation on all currently managed resources.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void EnableValidationAll();

    /**
     * Disables client-side schema validation on all currently managed resources.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void DisableValidationAll();

    /**
     * Marks all managed resources as having remote changes, forcing
     * the next access to re-fetch from the server.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void InvalidateAll();

    /**
     * Resets all connection-dependent state: invalidates all caches,
     * clears subscription flags, and removes notification listeners.
     *
     * Call on disconnect or before reconnecting. Subscription flags
     * are cleared locally without sending UnSub to the server (the
     * server session is assumed to be lost).
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void ResetConnectionState();

    /**
     * Controls whether subscribed resources are skipped during coarse invalidation.
     *
     * When true (the default), subscribed resources trust the server's notification
     * system and are not flagged by coarse-grained invalidation on non-idempotent
     * requests. When false, ALL managed resources are flagged on every non-idempotent
     * request, regardless of subscription status.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @param enabled                       True to skip subscribed resources during invalidation.
     */
    void SetSubscriptionOptimizedInvalidation(bool enabled);

    /**
     * Returns whether subscription-optimized invalidation is enabled.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @return                              True if subscribed resources are skipped during invalidation.
     */
    bool SubscriptionOptimizedInvalidation() const;

    /**
     * Returns the GoRestClient this manager is associated with.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @return                              Reference to the GoRestClient.
     */
    GoRestClient& Client();

    /**
     * Removes the managed resource for the given URI.
     *
     * If no external shared_ptr is held, the GoResource is destroyed.
     * Callers holding a shared_ptr will keep the instance alive until they
     * release it, but the manager will no longer track it.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     * @param uri                           The resource URI to remove.
     */
    void Remove(const std::string& uri);

    /**
     * Removes all managed resources that are marked as deleted.
     *
     * Call periodically or after bulk operations to reclaim memory from
     * resources that have been deleted but remain in the manager's map.
     *
     * @public                              @memberof GoResourceManager
     * @version                             Introduced in 1.5.0.0
     */
    void Cleanup();

private:
    friend class GoResource;

    /**
     * Called via GoRestClient's non-idempotent request callback.
     *
     * If subscriptionOptimizedInvalidation_ is true, skips subscribed resources.
     * Otherwise, flags all managed resources.
     */
    void OnNonIdempotentRequest();

    /**
     * Marks the resource at the given URI as deleted and removes it from the map.
     * No-op if the URI is not in the map.
     */
    void MarkAndRemoveIfPresent(const std::string& uri);

    GoRestClient& client_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<GoResource>> managedResources_;
    std::atomic<bool> autoSubscribe_{false};
    std::atomic<bool> autoValidation_{false};
    std::atomic<bool> subscriptionOptimizedInvalidation_{true};
};

}

#endif
