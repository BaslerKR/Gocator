#ifndef GOAPI_AUDIT_LOGGING_H
#define GOAPI_AUDIT_LOGGING_H

#include <GoApi/GoApiDef.h>
#include <GoApi/GoDataTree/GoDataTree.h>
#include <functional>
#include <string>

class AuditLoggingTests;

// Audit log option bitmask.
#define GO_LOG_OPTION_GOCAPP_AUDIT_MASK             (0x0000000010000000)

// RFC 5424 severity values for audit logs (Section 6.2.1).
#define GO_LOG_OPTION_AUDIT_LEVEL_ERROR             (0x3)   // RFC 5424 Error (3).
#define GO_LOG_OPTION_AUDIT_LEVEL_WARN              (0x4)   // RFC 5424 Warning (4).
#define GO_LOG_OPTION_AUDIT_LEVEL_INFO              (0x6)   // RFC 5424 Informational (6).

// Combined audit option defines (used by GoLogAudit* macros).
// Bit composition: GO_LOG_OPTION_GOCAPP_MASK | GO_LOG_OPTION_GOCAPP_AUDIT_MASK | severity
#define GO_LOG_OPTION_GOCAPP_AUDIT_INFO             ((kLogOption) 0x8000000010000006)  // GOCAPP + AUDIT + INFO(6)
#define GO_LOG_OPTION_GOCAPP_AUDIT_WARN             ((kLogOption) 0x8000000010000004)  // GOCAPP + AUDIT + WARN(4)
#define GO_LOG_OPTION_GOCAPP_AUDIT_ERROR            ((kLogOption) 0x8000000010000003)  // GOCAPP + AUDIT + ERROR(3)

namespace Go
{
    /**
     * Static API for audit logging operations.
     *
     * This class provides a lightweight dispatch layer for audit logs, routing calls
     * from GoApi to GoCommon::AuditLogger via dependency inversion (similar to how
     * Logging routes to GcLogger). This allows GoApi to emit audit logs without
     * depending on GoCommon.
     *
     * Typical usage:
     *   GoLogAuditRequest(Go::AuditLogging::AUDIT_METHOD_UPDATE, "/api/sensor/exposure", payloadTree, argsTree);
     */
    class AuditLogging
    {

    friend class ::AuditLoggingTests;

    public:
        //==================================================================
        // Audit Log API
        //==================================================================

        /**
         * Sends a structured audit log via syslog.
         *
         * Routes directly to the registered audit forwarder (AuditLogger),
         * bypassing kLog entirely. The severity (RFC 5424) is encoded in
         * the kLogOption bits — callers should use the GoLogAuditInfo/Warn/Error
         * macros rather than calling this function directly.
         *
         * @param   options         Log option flags (GO_LOG_OPTION_GOCAPP_AUDIT_*).
         * @param   msg             Human-readable audit message.
         * @param   structuredData  Pointer to GoDataTree with SD-ELEMENTs, or nullptr.
         * @param   msgId           RFC 5424 MSGID (default "-" = NILVALUE).
         * @return                  kOK on success, error code otherwise.
         */
        static GoApiCppFx(kStatus) Go_Audit_Log(kLogOption options,
                                                const std::string& msg,
                                                const GoApi::GoDataTree* structuredData = nullptr,
                                                const std::string& msgId = "-");

        /**
         * Log an audit event for a REST API request.
         *
         * @param   method      One of AUDIT_METHOD_* constants.
         * @param   path        REST resource path.
         * @param   payload     Request body as GoDataTree.
         * @param   args        Request args as GoDataTree.
         */
        static GoApiCppFx(void) LogAuditRequest(const std::string& method,
                                                const std::string& path,
                                                const GoApi::GoDataTree& payload = GoApi::GoDataTree(),
                                                const GoApi::GoDataTree& args = GoApi::GoDataTree());

        /**
         * Log a failed REST API request.
         *
         * @param   method      The method that failed (reqName or AUDIT_METHOD_*).
         * @param   path        REST resource path.
         * @param   error       Exception message (e.what()).
         */
        static GoApiCppFx(void) LogAuditRequestFailed(const std::string& method,
                                                      const std::string& path,
                                                      const std::string& error = "");

        //==================================================================
        // Callback type for audit forwarder (registered by AuditLogger)
        //==================================================================

        using AuditLogForwardFn = std::function<void(kLogOption options,
                                                     const std::string& msg,
                                                     const GoApi::GoDataTree* structuredData,
                                                     const std::string& msgId)>;

        /**
         * Register audit log forwarder callback (dependency inversion: GoApi → GoCommon).
         * Called by AuditLogger to register/unregister its forwarding callback.
         *
         * @param   fn  Callback function pointer, or nullptr to unregister.
         */
        static GoApiCppFx(void) SetAuditLogCallback(AuditLogForwardFn fn);

        /**
         * Set audit logging enabled state (mirrors AuditLogger master switch).
         * Called by AuditLogger when configuration changes.
         *
         * @param   enabled  true to enable audit logging, false to disable.
         */
        static GoApiCppFx(void) SetAuditEnabled(bool enabled);

        /**
         * Check if audit logging is enabled.
         *
         * @return  true if audit logging is active, false otherwise.
         */
        static GoApiCppFx(bool) AuditEnabled();

        //==========================================================================
        // Common SD-IDs for audit structured data
        //==========================================================================

        static constexpr const char* AUDIT_SD_ID_REQUEST            = "auditRequest";

        //==========================================================================
        // Common SD-PARAMs for audit structured data
        //==========================================================================

        static constexpr const char* AUDIT_SD_PARAM_METHOD          = "method";
        static constexpr const char* AUDIT_SD_PARAM_PATH            = "path";
        static constexpr const char* AUDIT_SD_PARAM_PAYLOAD         = "payload";
        static constexpr const char* AUDIT_SD_PARAM_ARGS            = "args";
        static constexpr const char* AUDIT_SD_PARAM_ERROR           = "error";

        //==========================================================================
        // Common method values for audit SD-PARAM "method"
        //==========================================================================

        static constexpr const char* AUDIT_METHOD_CALL              = "call";
        static constexpr const char* AUDIT_METHOD_CREATE            = "create";
        static constexpr const char* AUDIT_METHOD_DELETE            = "delete";
        static constexpr const char* AUDIT_METHOD_READ              = "read";
        static constexpr const char* AUDIT_METHOD_UPDATE            = "update";

        //==========================================================================
        // Common MSGID values (RFC 5424 MSGID field)
        //==========================================================================

        //--------------------------------------------------------------------------
        static constexpr const char* AUDIT_MSGID_SYSTEM_STARTUP     = "SYSTEM_STARTUP";
        static constexpr const char* AUDIT_MSGID_SYSTEM_SHUTDOWN    = "SYSTEM_SHUTDOWN";
        //--------------------------------------------------------------------------
        static constexpr const char* AUDIT_MSGID_DATA_ACCESS        = "DATA_ACCESS";
        static constexpr const char* AUDIT_MSGID_CONFIG_CHANGE      = "CONFIG_CHANGE";
        static constexpr const char* AUDIT_MSGID_COMMAND_EXEC       = "COMMAND_EXEC";
        static constexpr const char* AUDIT_MSGID_REQUEST_FAILURE    = "REQUEST_FAILURE";
        //--------------------------------------------------------------------------
        // TBD:
        //static constexpr const char* AUDIT_MSGID_USER_LOGIN         = "USER_LOGIN";
        //static constexpr const char* AUDIT_MSGID_USER_LOGOUT        = "USER_LOGOUT";
        //static constexpr const char* AUDIT_MSGID_JOB_LOAD           = "JOB_LOAD";
        //static constexpr const char* AUDIT_MSGID_JOB_SAVE           = "JOB_SAVE";
        //static constexpr const char* AUDIT_MSGID_FIRMWARE_UPDATE    = "FIRMWARE_UPDATE";
        //static constexpr const char* AUDIT_MSGID_SCAN_START         = "SCAN_START";
        //static constexpr const char* AUDIT_MSGID_SCAN_STOP          = "SCAN_STOP";
        //static constexpr const char* AUDIT_MSGID_AUTH_FAILURE       = "AUTH_FAILURE";
        //--------------------------------------------------------------------------

    private:
        /**
         * Summarizes a GoDataTree into a string for audit logging with size limiting.
         *
         * Sanitizes the tree via SanitizeTreeRecursive() to replace binary nodes
         * with "Binary" placeholders, then serializes the result to JSON. The JSON
         * output is size-limited to keep RFC 5424 SD-PARAM values concise.
         *
         * @param   tree            GoDataTree to be summarized.
         * @return                  A string summary of the tree:
         * - If JSON length <= AUDIT_PAYLOAD_MAX_LEN: returns the JSON string.
         * - If JSON length > AUDIT_PAYLOAD_MAX_LEN: returns "JSON (x bytes)".
         * - On error: returns "Unknown content".
         */
        static GoApiCppFx(std::string) SummarizeTreeForAudit(const GoApi::GoDataTree& tree);

        /**
         * Recursively creates a sanitized copy of a GoDataTree with binary nodes replaced
         * by "Binary" placeholders.
         *
         * REST request trees may contain large binary data (e.g., firmware packages,
         * file uploads) nested at arbitrary depth within objects or arrays. A top-level
         * IsBinary() check is insufficient because the tree itself is an object — only
         * a child field holds the binary data. Calling GoDataTree::ToJson() on such a
         * tree without sanitization attempts to serialize the binary content (potentially
         * hundreds of megabytes), causing std::bad_alloc.
         *
         * This function traverses the tree recursively, replacing binary nodes with
         * "Binary" string placeholders while preserving all non-binary fields so the
         * audit log captures the full request structure
         * (e.g., {skipValidation: false, package: "Binary"}).
         *
         * @param   tree            GoDataTree to sanitize.
         * @return                  Sanitized copy with binary nodes replaced by "Binary" strings.
         */
        static GoApiCppFx(GoApi::GoDataTree) SanitizeTreeRecursive(const GoApi::GoDataTree& tree);

        static AuditLogForwardFn auditForwarder;
    };
}

//==================================================================
// Audit logging macros
//==================================================================

/**
 * Helper macro to log an informational audit message with optional structured data and MSGID.
 *
 * @param   msg             Human-readable audit message (std::string or const char*).
 * @param   ...             Optional: &structuredData (GoDataTree*), msgId (const char*).
 *
 * Examples:
 *   GoLogAuditInfo("System started");
 *   GoLogAuditInfo("Configuration changed", &sd);
 *   GoLogAuditInfo("REST API read", &sd, Go::AuditLogging::AUDIT_MSGID_DATA_ACCESS);
 */
#define GoLogAuditInfo(msg, ...)                        Go::AuditLogging::Go_Audit_Log(GO_LOG_OPTION_GOCAPP_AUDIT_INFO, msg, ## __VA_ARGS__)

/**
 * Helper macro to log a warning audit message with optional structured data and MSGID.
 *
 * @param   msg             Human-readable audit message (std::string or const char*).
 * @param   ...             Optional: &structuredData (GoDataTree*), msgId (const char*).
 *
 * Examples:
 *   GoLogAuditWarn("Storage threshold exceeded");
 *   GoLogAuditWarn("Invalid configuration detected", &sd);
 *   GoLogAuditWarn("Deprecated API called", &sd, Go::AuditLogging::AUDIT_MSGID_CONFIG_CHANGE);
 */
#define GoLogAuditWarn(msg, ...)                        Go::AuditLogging::Go_Audit_Log(GO_LOG_OPTION_GOCAPP_AUDIT_WARN, msg, ## __VA_ARGS__)

/**
 * Helper macro to log an error audit message with optional structured data and MSGID.
 *
 * @param   msg             Human-readable audit message (std::string or const char*).
 * @param   ...             Optional: &structuredData (GoDataTree*), msgId (const char*).
 *
 * Examples:
 *   GoLogAuditError("Authentication failed");
 *   GoLogAuditError("Database connection lost", &sd);
 *   GoLogAuditError("Critical security violation", &sd, Go::AuditLogging::AUDIT_MSGID_REQUEST_FAILURE);
 */
#define GoLogAuditError(msg, ...)                       Go::AuditLogging::Go_Audit_Log(GO_LOG_OPTION_GOCAPP_AUDIT_ERROR, msg, ## __VA_ARGS__)

/**
 * Helper macro to log a REST API request with automatic MSGID mapping.
 *
 * @param   method          One of AUDIT_METHOD_* constants.
 * @param   path            REST resource path.
 * @param   ...             Optional: payload (GoDataTree), args (GoDataTree).
 *
 * Examples:
 *   GoLogAuditRequest(Go::AuditLogging::AUDIT_METHOD_READ, "/system");
 *   GoLogAuditRequest(Go::AuditLogging::AUDIT_METHOD_UPDATE, "/audit", payloadTree);
 *   GoLogAuditRequest(Go::AuditLogging::AUDIT_METHOD_CALL, "/jobs", payloadTree, argsTree);
 */
#define GoLogAuditRequest(method, path, ...)            Go::AuditLogging::LogAuditRequest(method, path, ## __VA_ARGS__)

/**
 * Helper macro to log a failed REST API request.
 *
 * @param   method          The method that failed (reqName or AUDIT_METHOD_*).
 * @param   path            REST resource path.
 * @param   err             Exception message (e.what()).
 *
 * Examples:
 *   GoLogAuditRequestFailed(Go::AuditLogging::AUDIT_METHOD_UPDATE, "/system/network", e.what());
 *   GoLogAuditRequestFailed("call", "/tools/scan", "Timeout waiting for scan completion");
 */
#define GoLogAuditRequestFailed(method, path, err)      Go::AuditLogging::LogAuditRequestFailed(method, path, err)

#endif
