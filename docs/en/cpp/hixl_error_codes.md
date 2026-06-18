# HIXL Error Codes

Error codes are defined as follows. The type is `uint32_t`.

```
// status codes
constexpr Status SUCCESS = 0U;
constexpr Status PARAM_INVALID = 103900U;
constexpr Status TIMEOUT = 103901U;
constexpr Status NOT_CONNECTED = 103902U;
constexpr Status ALREADY_CONNECTED = 103903U;
constexpr Status NOTIFY_FAILED = 103904U;
constexpr Status UNSUPPORTED = 103905U;
constexpr Status FAILED = 503900U;
constexpr Status RESOURCE_EXHAUSTED = 203900U;
```

The meanings of error codes are as follows.

| Enumerated Value| Description| Recoverable| Solution|
| --- | --- | --- | --- |
| SUCCESS | Successful| None| N/A|
| PARAM_INVALID | Incorrect parameters| Yes| Locate the fault based on logs.|
| TIMEOUT | Process timed out| No| Preserve the environment, collect Host/Device logs, and back them up.|
| NOT_CONNECTED | No link established| Yes| Verify the link status at the upper layer.|
| ALREADY_CONNECTED | Link established| Yes| Verify the link status at the upper layer.|
| NOTIFY_FAILED | Notification failed| No| Reserved error code. It will not be returned.|
| UNSUPPORTED | Unsupported parameter or API| Yes| Reserved error code. It will not be returned.|
| FAILED | Common failure| No| Preserve the environment, collect Host/Device logs, and back them up.|
| RESOURCE_EXHAUSTED | Resources exhausted, only stream resources left| Yes| Try again after the resources are released.|
