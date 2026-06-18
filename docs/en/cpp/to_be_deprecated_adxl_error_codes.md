# To Be Deprecated_ADXL Error Codes

Error codes are defined as follows. The type is `uint32_t`.

```
// Status codes
constexpr Status SUCCESS = 0U;
constexpr Status PARAM_INVALID = 103900U;
constexpr Status TIMEOUT = 103901U;
constexpr Status NOT_CONNECTED = 103902U;
constexpr Status ALREADY_CONNECTED = 103903U;
constexpr Status NOTIFY_FAILED = 103904U;
constexpr Status UNSUPPORTED = 103905U;
constexpr Status FAILED = 503900U;
```

The meanings of error codes are as follows.

|Enumerated Value|Meaning|Recoverable|Solution|
|--|--|--|--|
|SUCCESS|Successful|No|N/A|
|PARAM_INVALID|Incorrect parameter|Yes|Locate the fault based on logs.|
|TIMEOUT|Process timed out|No|Preserve the environment, collect Host/Device logs, and back them up.|
|NOT_CONNECTED|No link established|Yes|Verify the link status at the upper layer.|
|ALREADY_CONNECTED|Link established|Yes|Verify the link status at the upper layer.|
|NOTIFY_FAILED|Notification failed|No|Reserved error code. It will not be returned.|
|UNSUPPORTED|Unsupported parameter or API|Yes|Reserved error code. It will not be returned.|
|FAILED|Common failure|No|Preserve the environment, collect Host/Device logs, and back them up.|
