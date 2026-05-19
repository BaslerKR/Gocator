/**
 * @file    GoRestClient.h
 * @brief   Declares the GoPxLSdk.GoRestClient class.
 *
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GORESTCLIENT_H
#define GO_PXL_SDK_GORESTCLIENT_H

#include <kApi/Io/kNetwork.h>

#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoNotificationType.h>
#include <GoPxLSdk/GoResponse.h>
#include <GoPxLSdk/GoTransaction.h>
#include <GoPxLSdk/Internal/GoCtrlChannel.h>
#include <GoPxLSdk/Internal/V1/GoCtrlChannelV1.h>

#include <atomic>
#include <unordered_map>

class GoRestClientTests;

namespace GoPxLSdk
{
/**
 * Notification handler used in GoRestClient::Sub.
 * The notification object is passed into the receiving function.
 */
using GoNotificationHandler = std::function<void(const std::shared_ptr<GoNotificationResponse>&)>;

/**
 * Notification listener callback, same signature as GoNotificationHandler.
 */
using NotificationCallback = std::function<void(const std::shared_ptr<GoNotificationResponse>&)>;

/**
 * Notification listener ID, returned by AddNotificationListener.
 */
using NotificationListenerId = size_t;

/**
 * Data handler used in GoRestClient::StartStream.
 * The stream message object is passed into the receiving function.
 */
using GoStreamHandler = std::function<void(const std::shared_ptr<GoStreamResponse>&)>;

class GoPxLSdkClass GoRestClient
{
public:
    /**
     * Constructs GoRestClient.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     */
    GoRestClient();

    ~GoRestClient() = default;

    /**
     * Connects to a REST endpoint at the provided IP address and port. Waits until the connection is established.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.1.50.15
     * @param address         The endpoint's IP address.
     * @param port            The endpoint's port. If 0, the default port will be used.
     * @throws Go::Exception  If unable to connect.
     */
    void Connect(kIpAddress address, k16u port = 0);

    /**
     * Disconnects from a REST endpoint. Waits until the disconnection is complete.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @throws Go::Exception  If unable to disconnect.
     */
    void Disconnect();

    /**
     * Checks if the REST Client is connected or not.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @return                True if connected, false otherwise.
     */
    bool IsConnected() const;

    /**
     * Reads a resource and returns its data. Analogous to HTTP's GET method: there is no content/body,
     * and 'args' are similar to query strings.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Read(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Updates a resource with the provided content. Analogous to HTTP's PATCH method: there is content/body,
     * and 'args' are similar to query strings.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Update(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Creates a resource with the provided content. Analogous to HTTP's POST method: there is content/body,
     * and 'args' are similar to query strings.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Create(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Deletes a resource. Analogous to HTTP's DELETE method: there is no content/body, and 'args' are similar
     * to query strings.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Delete(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Calls a callable resource. Not all resources are callable, check the API docs.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Call(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Subscribes for notifications on an observable resource. Not all resources are observable, check the API docs.
     * Ensure you have set the subscription handler before calling this function to ensure you do not miss any
     * messages.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction Sub(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Unsubscribes for notifications on an observable resource. Not all resources are observable, check the API docs.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction UnSub(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Sets the callback handler to receive subscription notifications.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param callback        The callback which receives subscription notifications.
     */
    void SetSubHandler(const GoNotificationHandler& callback);

    /**
     * Start streaming a streamable resource. Not all resources are streamable, check the API docs.
     * Ensure you have set the stream handler before calling this function to ensure you do not miss any
     * messages.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction StartStream(const std::string & uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Stops streaming a streamable resource. Not all resources are streamable, check the API docs.
     * 
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param uri             The URI to the resource.
     * @param content         GoJson object of the content for the request.
     * @param args            GoJson object of arguments for the request.
     * @throw GoChannelError  If not connected to channel.
     * @return                Transaction object to monitor status and wait for results.
     */
    GoTransaction StopStream(const std::string& uri, const GoJson& content = {}, const GoJson& args = {});

    /**
     * Sets the callback handler to receive stream messages.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 0.2.1.53.
     * @param callback        The callback which receives stream messages.
     */
    void SetStreamHandler(const GoStreamHandler& callback);

    /**
     * Sets the callback handler to receive internal read errors.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.2.1.114
     * @param callback        The callback which receives internal read errors.
     */
    void SetReadErrorHandler(const GoCtrlChannelReadErrorHandler& callback);

    /**
     * Registers a callback invoked when a notification arrives matching the given URI.
     * Returns an ID for later removal. Multiple listeners per URI are supported.
     * Callbacks are invoked under a lock and must be fast (e.g., set an atomic flag).
     *
     * Server-side subscription management: When the first listener is added for a
     * given URI, GoRestClient automatically sends Sub to the server. When the last
     * listener for a URI is removed via RemoveNotificationListener, GoRestClient
     * sends UnSub. Callers never need to call Sub/UnSub directly when using the
     * listener API.
     *
     * Thread-safe.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     * @param uri             The URI to listen for notifications on.
     * @param callback        The callback to invoke when a matching notification arrives.
     * @return                Listener ID for use with RemoveNotificationListener.
     */
    NotificationListenerId AddNotificationListener(
        const std::string& uri,
        NotificationCallback callback);

    /**
     * Removes a previously registered notification listener.
     * Blocks until any in-flight callback for this listener completes.
     * If this was the last listener for the URI, sends UnSub to the server.
     * Thread-safe.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     * @param id              The listener ID returned by AddNotificationListener.
     */
    void RemoveNotificationListener(NotificationListenerId id);

    /**
     * Replaces the callback for an existing notification listener.
     * Does not send any network requests. Used by GoResource move operations
     * to re-bind the callback to the new `this` pointer.
     * Thread-safe.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     * @param id              The listener ID returned by AddNotificationListener.
     * @param callback        The new callback to use.
     */
    void ReplaceListenerCallback(NotificationListenerId id, NotificationCallback callback);

    /**
     * Registers a callback invoked after any non-idempotent request
     * (Update, Create, Delete, Call) is sent. Only one handler at a time;
     * replaces any previous handler.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     * @param handler         The callback to invoke after non-idempotent requests.
     */
    void SetNonIdempotentRequestHandler(std::function<void()> handler);

    /**
     * Registers a callback invoked when a connection loss is detected
     * (read error on the channel). Only one handler at a time; replaces
     * any previous handler. Fires at most once per connection session;
     * resets on Connect().
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     * @param handler         The callback to invoke on connection loss.
     */
    void SetDisconnectHandler(std::function<void()> handler);

    /**
     * Removes all notification listeners without sending UnSub to the server.
     *
     * For use during disconnection when the server session is already lost
     * and sending network messages is not possible or desired.
     *
     * @public                @memberof GoRestClient
     * @version               Introduced in 1.5
     */
    void ClearAllListeners();

private:
    void ResponseHandler(std::shared_ptr<GoResponse> response);

    void WriteErrorHandler(k64u requestId, const std::exception& e);

    void Callback(std::shared_ptr<GoResponse> response);

    /**
     Locks when GoRestClient instance command is being used by a thread.
     */
    std::mutex commandMutex;

    // Using std::deque instead of std::queue, because it provides iterator used by `HandleReadError` and `HandleWriteError`.
    std::deque<GoTransaction> transactionQueue;

    std::shared_ptr<GoCtrlChannel> channel;
    
    void DispatchNotification(const std::shared_ptr<GoNotificationResponse>& notification);

    GoStreamHandler streamHandler;
    GoNotificationHandler notificationHandler;

    std::mutex listenerMutex_;
    size_t nextListenerId_ = 1;
    std::unordered_map<std::string, std::vector<std::pair<size_t, NotificationCallback>>> listenersByUri_;
    std::unordered_map<size_t, std::string> listenerIdToUri_;
    std::function<void()> nonIdempotentRequestHandler_;
    std::function<void()> disconnectHandler_;
    GoCtrlChannelReadErrorHandler userReadErrorHandler_;
    std::atomic<bool> disconnectHandlerFired_{false};

    void OnReadError(ReadErrorType readErrorType, const std::exception& e);

    friend class ::GoRestClientTests;
};

}

#endif
