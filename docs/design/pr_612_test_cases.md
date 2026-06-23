## 测试用例（42 个）

### 端到端传输（4）

| 用例 | 验证点 |
|------|--------|
| `HixlEngineUbgTest.EndToEndUbgBatchTransferHostToHost` | UBG H2H 双向传输（READ+WRITE） |
| `HixlEngineUbgTest.EndToEndUbgBatchTransferHostToDevice` | UBG H2D 双向传输 |
| `HixlEngineUbgTest.EndToEndUbgBatchTransferDeviceToHost` | UBG D2H 双向传输 |
| `HixlEngineUbgTest.EndToEndUbgBatchTransferDeviceToDevice` | UBG D2D 双向传输 |

### Endpoint 生成（14）

| 用例 | 验证点 |
|------|--------|
| `BuildEndpointListWithIntraRoceEnabledOnlyReturnsRoce` | HCCL\_INTRA\_ROCE\_ENABLE=1 过滤非 RoCE 端点 |
| `BuildEndpointListGeneratesUbgWhenConfiguredOnA5` | protocol\_desc=ubg → 生成 UBG EID + net\_instance\_id |
| `BuildEndpointListFromOptionsGeneratesUboeWhenConfiguredOnA5` | protocol\_desc=uboe → 生成 UBoE IP + net\_instance\_id |
| `AutoGenUbgByInterconTypeWhenNoProtocolDescOnA5` | 无 protocol\_desc + InterconType=4 → 自动生成 UBG |
| `AutoGenUboeByInterconTypeWhenNoProtocolDescOnA5` | 无 protocol\_desc + InterconType=2 → 自动生成 UBoE |
| `RejectsUbgProtocolDescWhenInterconTypeIsUboe` | 配 ubg 但 InterconType=2 → FAILED |
| `FailsToGenUbgWhenNoUbgeid` | DCMI 无 UBG EID → FAILED |
| `RejectsScaleOutProtocolWithHostPlacement` | local\_comm\_res 配 ubg:host → 拒绝 |
| `RejectsUbgHostPlacementInProtocolDesc` | protocol\_desc 配 ubg:host → 拒绝 |
| `RejectsUbgAndUboeCoexistenceInLocalCommRes` | local\_comm\_res 同时含 ubg+uboe → 拒绝 |
| `RejectsProtocolDescWithBothUbgAndUboe` | protocol\_desc 同时配 ubg+uboe → 拒绝 |
| `ConvertToEndpointDescDeviceUbgParsesEidTest` | UBG EID 解析 + FillDeviceLocInfo 填充 |
| `ConvertToEndpointDescDeviceUbgRejectsInvalidEidTest` | UBG 无效 EID → 失败 |
| `EngineFactoryUsesHixlEngineWhenProtocolDescConfigured` | protocol\_desc 配置时使用 HixlEngine |

### 协议匹配（13）

| 用例 | 验证点 |
|------|--------|
| `EndpointMatcherCrossInstancePrefersUboe` | 跨实例：UBoE > UBG > RoCE |
| `EndpointMatcherCrossInstancePrefersUbgWhenNoUboe` | 跨实例：无 UBoE 时选 UBG |
| `EndpointMatcherCrossInstanceFallsBackToDeviceRoce` | 跨实例：无 ScaleOut 时回退 RoCE |
| `EndpointMatcherSameInstancePrefersHccs` | 同实例：HCCS 最高优先 |
| `EndpointMatcherSameInstanceFallsBackToUboe` | 同实例：无 HCCS 选 UBoE |
| `EndpointMatcherSameInstanceFallsBackToUbg` | 同实例：无 HCCS/UBoE 选 UBG |
| `EndpointMatcherSameInstanceFallsBackToDeviceRoce` | 同实例：全部无时回退 RoCE |
| `EndpointMatcherForceRoceWhenEnvSet` | HCCL\_INTRA\_ROCE\_ENABLE=1 强制 RoCE |
| `EndpointMatcherSameInstancePrefersUbBeforeScaleOut` | 同实例：UB 优先于 ScaleOut |
| `EndpointMatcherSameInstanceUbPreemptsDirectPriority` | UB 匹配优先于直连协议 |
| `EndpointMatcherDirectMatchRequiresSamePlacement` | 直连匹配要求 placement 相同 |
| `EndpointMatcherDstEidPriorityTest` | dst\_eid 匹配优先 |
| `EndpointMatcherAllDstEidEmptyTest` | dst\_eid 全空时正常匹配 |

### Endpoint Store（4）

| 用例 | 验证点 |
|------|--------|
| `MatchEndpointSucceedsForUbTpByEid` | UB\_TP 相同 EID → 匹配复用 |
| `MatchEndpointFailsForUbCtpWhenEidDiffers` | UB\_CTP 不同 EID → 不匹配 |
| `MatchEndpointFailsForUbgWhenEidDiffers` | UBG 不同 EID → 不匹配 |
| `MatchEndpointSucceedsForUbgWhenEidSame` | UBG 相同 EID → 匹配复用 |

### Host 地址转换（2）

| 用例 | 验证点 |
|------|--------|
| `ConvertHostMappedDescsFailsIfRemoteRegisteredInWrongRegion` | 远端地址注册区域错误 → 转换失败 |
| `ConvertHostMappedDescsFailsIfLocalRegisteredInWrongRegion` | 本端地址注册区域错误 → 转换失败 |

### Proxy 接口（5）

| 用例 | 验证点 |
|------|--------|
| `DsmiProxyUt.LoadFailureThenSuccess` | DSMI dlopen/dlsym 失败→成功 + GetInterconType 查询 |
| `DsmiProxyUt.IsInterconTypeSupportedAfterLoad` | 加载后 IsInterconTypeSupported 返回 true |
| `DcmiProxySuccessTest.GetDeviceInfoSuccess` | DCMI GetDeviceInfo 成功 |
| `DcmiProxyLoadFailTest.GetMainboardIdReturnsErrorWhenDcmiFuncFails` | DCMI 函数返回错误时代理返回 -1 |
| `DcmiProxyLoadFailTest.UnloadDcmiWhenNotLoaded` | 未加载时 Unload 安全 |
