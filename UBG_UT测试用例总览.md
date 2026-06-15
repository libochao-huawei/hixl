# UBG MR 测试用例总览

## 1. UBG 端到端传输（`hixl_engine_ubg_unittest.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `EndToEndUbgBatchTransferHostToHost` | UBG 协议 H2H 传输全流程 |
| `EndToEndUbgBatchTransferHostToDevice` | UBG 协议 H2D 传输全流程 |
| `EndToEndUbgBatchTransferDeviceToHost` | UBG 协议 D2H 传输全流程 |
| `EndToEndUbgBatchTransferDeviceToDevice` | UBG 协议 D2D 传输全流程 |

## 2. Endpoint 生成（`endpoint_generator_ut.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `BuildEndpointListFromOptionsGeneratesUboeWhenConfiguredOnA5` | A5 + protocol_desc=uboe → 生成 UBoE endpoint + net_instance_id="superpod_8" |
| `BuildEndpointListGeneratesUbgWhenConfiguredOnA5` | A5 + protocol_desc=ubg → 生成 UBG endpoint + net_instance_id="superpod_0" |
| `RejectsScaleOutProtocolWithHostPlacement` | local_comm_res 中 ubg:host → PARAM_INVALID |
| `RejectsUbgAndUboeCoexistenceInLocalCommRes` | local_comm_res 同时含 ubg+uboe → PARAM_INVALID |
| `RejectsProtocolDescWithBothUbgAndUboe` | protocol_desc 同时配 uboe+ubg → PARAM_INVALID（kConflict） |
| `ConvertToEndpointDescDeviceUbgParsesEidTest` | UBG endpoint → ParseEidAddress + FillDeviceLocInfo |
| `ConvertToEndpointDescDeviceUbgRejectsInvalidEidTest` | UBG 无效 EID（长度不对）→ 失败 |

## 3. 协议优先级匹配（`hixl_client_unittest.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `EndpointMatcherCrossSuperNodePrefersUboe` | 跨超节点：UBoE > UBG > RoCE |
| `EndpointMatcherCrossSuperNodePrefersUbgWhenNoUboe` | 跨超节点：无 UBoE 时选 UBG |
| `EndpointMatcherCrossSuperNodeFallsBackToDeviceRoce` | 跨超节点：无 ScaleOut 时回退 RoCE |
| `EndpointMatcherIntraSuperNodePrefersHccs` | 超节点内：HCCS > UBoE > UBG > RoCE |
| `EndpointMatcherIntraSuperNodeFallsBackToUboe` | 超节点内：无 HCCS 选 UBoE |
| `EndpointMatcherIntraSuperNodeFallsBackToUbg` | 超节点内：无 HCCS/UBoE 选 UBG |
| `EndpointMatcherIntraSuperNodeFallsBackToDeviceRoce` | 超节点内：全部无时回退 RoCE |
| `EndpointMatcherProtocolLockOnlyMatchesLockedProtocol` | protocol_lock 过滤非锁定协议 |
| `EndpointMatcherProtocolLockUboeForcesUboe` | protocol_lock=kUboe 强制只匹配 UBoE |
| `EndpointMatcherForceRoceWhenEnvSet` | HCCL_INTRA_ROCE_ENABLE=1 强制只走 RoCE |
| `EndpointMatcherSameSuperNodePrefersUbBeforeScaleOut` | 超节点内：UB 优先于 ScaleOut |

## 4. ProtocolLock 引擎级验证（`hixl_engine_unittest.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `TestInitializeSetsProtocolLockNoneForExplicitEndpoints` | 显式 endpoint → protocol_lock=kNone |
| `TestInitializeSetsProtocolLockUboeForProtocolDescGenerated` | protocol_desc=uboe → protocol_lock=kUboe |
| `TestInitializeSetsProtocolLockUbgForProtocolDescGenerated` | protocol_desc=ubg → protocol_lock=kUbg |

## 5. Endpoint Store（`endpoint_store_ut.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `MatchEndpointFailsForUbgWhenEidDiffers` | UBG endpoint EID 不匹配 → 匹配失败 |

## 6. CS Client Host 映射（`hixl_cs_client_ub_ut.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `ConvertHostMappedDescsFailsIfRemoteRegisteredInWrongRegion` | UBG host 内存地址转换失败（远端区域错误） |
| `ConvertHostMappedDescsFailsIfLocalRegisteredInWrongRegion` | UBG host 内存地址转换失败（本端区域错误） |

## 7. DSMI Proxy（`dsmi_proxy_ut.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `LoadFailureThenSuccess` | dlopen 失败 → dlsym 失败 → 成功（stub）→ 函数返回错误 |
| `IsInterconTypeSupportedAfterLoad` | 加载后 IsInterconTypeSupported 返回 true |

## 8. DCMI Proxy（`dcmi_proxy_ut.cc`）

| 测试用例 | 覆盖场景 |
|---------|---------|
| `GetMainboardIdReturnsErrorWhenDcmiFuncFails` | DCMI 函数返回错误时代理返回 -1（替代原 dlopen 失败测试） |

## 9. UBoE 测试适配（`hixl_engine_uboe_unittest.cc`）

| 改动 | 覆盖场景 |
|------|---------|
| 加 `DsmiStubSetInterconType(2U)` | UBoE 测试 SetUp 设置正确的 InterconType |

---

**合计：新增/修改约 30 个测试用例，覆盖 UBG 生成、匹配、传输、参数校验、DSMI/DCMI 代理全链路。**
