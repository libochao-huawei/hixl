# 不可信入参参数校验规范

> **适用场景**：当函数入参来自不可信模块（如 device kernel 入口、`extern "C"` 导出函数、Python 绑定、跨进程 RPC 回调等）时，必须在函数入口处完成完整的参数校验，防止非法输入导致崩溃、任意地址读写或资源耗尽。

## 信任边界识别

以下函数签名模式视为信任边界入口，入参校验为强制要求：

| 入口类型 | 识别特征 | 示例 |
|---------|---------|------|
| Device kernel 入口 | `extern "C"` + kernel 函数名 | `HixlBatchPut(HixlOneSideOpParam *param)` |
| 公开 C API | `extern "C"` 或 `include/` 下头文件声明 | `HixlCSServerCreate(...)` |
| Python 绑定 | `pybind11` module def | `m.def("batch_put", ...)` |
| 跨进程回调 | RPC/消息处理函数 | `OnMessage(...)` |

## 校验检查清单

对每个信任边界入口函数，按以下顺序逐项检查：

| 编号 | 检查项 | 校验要求 | 违规后果 |
|------|--------|---------|---------|
| P1 | 指针参数判空 | 所有指针/句柄参数在使用前必须判空 | 空指针解引用导致崩溃 |
| P2 | 地址字段非零 | 结构体中以整数存储的地址字段（如 `xxx_addr`）必须校验非 0 | `reinterpret_cast` 后变成空指针或非法地址访问 |
| P3 | 数量/长度范围 | 表示数量、长度、大小的整数字段必须校验 `> 0` 且 `<= 上限` | 0 值导致空循环或 OOM；极大值导致整数溢出或 OOM |
| P4 | 校验失败阻断执行 | 校验失败必须立即返回错误码，不能继续执行后续逻辑 | 非法参数透传到底层，可能触发更严重的问题 |
| P5 | 子结构体字段校验 | 通过地址字段间接访问的子结构体数组，每个元素的指针和长度字段也需校验 | 非法指针/长度透传给传输层 |
| P6 | 校验前不触碰参数 | 校验完成前不得对参数做任何解引用或类型转换 | 校验前的非法访问已经造成崩溃 |

## 检查方法

### 1. 定位信任边界入口

在变更文件中搜索以下模式：

```
extern "C"
PYBIND11_MODULE
m\.def\(
```

以及 `include/` 目录下头文件中声明的公开函数。

### 2. 逐函数检查校验完整性

对每个入口函数：

1. 列出所有入参字段（含结构体成员）
2. 对每个字段判定校验类型（P1-P6）
3. 检查校验是否在首次使用前完成
4. 检查校验失败路径是否立即返回错误码

## 正确示例

```cpp
uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  // P1: 指针判空
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }
  // P3: 数量范围校验
  constexpr uint32_t kMaxBatchSize = 8192;
  if (param->list_num == 0 || param->list_num > kMaxBatchSize) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] invalid list_num=%u, valid range is [1, %u]",
              param->list_num, kMaxBatchSize);
    return PARAM_INVALID;
  }
  // P2: 地址字段非零校验
  if (param->op_desc_list_addr == 0) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] op_desc_list_addr is null");
    return PARAM_INVALID;
  }
  // P6: 校验完成后才开始使用参数
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(
      static_cast<uintptr_t>(param->op_desc_list_addr));
  ...
}
```

## 错误示例

```cpp
// 错误：未校验 list_num 和 op_desc_list_addr，直接解引用
uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  if (param == nullptr) {
    return PARAM_INVALID;
  }
  // 缺少 P2/P3 校验，直接 reinterpret_cast 并访问
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(
      static_cast<uintptr_t>(param->op_desc_list_addr));
  std::vector<HcommBatchTransferDesc> descs(param->list_num);  // list_num 未校验，可能 OOM
  for (uint32_t i = 0; i < param->list_num; i++) {
    descs[i].transferInfo.read.len = op_list[i].len;  // 可能越界
  }
  ...
}
```
