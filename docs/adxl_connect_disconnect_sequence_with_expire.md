# AdxlInnerEngine Connect 和 Disconnect 时序图

## Connect 流程时序图（显式包含 MsgHandlerPlugin）

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant Channel as Channel
    participant SegmentTable as SegmentTable

    Client->>Engine: Connect(remote, timeout)
    activate Engine
    Engine->>MsgHandler: Connect(remote, timeout)
    activate MsgHandler

    Note over MsgHandler: 检查是否已存在
    MsgHandler->>ChannelMgr: GetChannel(kClient, remote)
    ChannelMgr-->>MsgHandler: nullptr

    MsgHandler->>MsgHandler: ParseListenInfo(remote)

    Note over MsgHandler: 通过插件建立TCP
    MsgHandler->>Plugin: Connect(remote_ip, remote_port, timeout)
    Plugin-->>MsgHandler: conn_fd

    Note over MsgHandler: 交换连接信息
    MsgHandler->>Plugin: SendMsg(conn_fd, kConnect, connect_info)
    MsgHandler->>Plugin: RecvMsg(conn_fd, kConnect, peer_connect_info)

    Note over MsgHandler: 处理连接信息并创建Channel
    MsgHandler->>MsgHandler: ConnectInfoProcess(...)->ChannelInfo
    MsgHandler->>ChannelMgr: CreateChannel(channel_info)
    activate ChannelMgr
    ChannelMgr->>Channel: Initialize()
    activate Channel
    Channel-->>ChannelMgr: SUCCESS
    deactivate Channel
    ChannelMgr-->>MsgHandler: channel_ptr
    deactivate ChannelMgr

    Note over MsgHandler: 远端地址入SegmentTable
    MsgHandler->>SegmentTable: AddRange(...)

    Note over MsgHandler: 获取对端处理状态
    MsgHandler->>Plugin: RecvMsg(conn_fd, kStatus, status)

    alt status.error_code != SUCCESS
        MsgHandler-->>Engine: FAILED
        Engine-->>Client: FAILED
    else status.error_code == SUCCESS
        MsgHandler->>Channel: SetSocketNonBlocking(conn_fd)
        MsgHandler->>ChannelMgr: AddSocketToEpoll(conn_fd, channel)
        MsgHandler-->>Engine: SUCCESS
        Engine-->>Client: SUCCESS
    end

    deactivate MsgHandler
    deactivate Engine
```

## Disconnect 流程时序图（显式包含 MsgHandlerPlugin）

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant Channel as Channel

    Client->>Engine: Disconnect(remote, timeout)
    activate Engine
    Engine->>MsgHandler: Disconnect(remote, timeout)
    activate MsgHandler

    MsgHandler->>ChannelMgr: GetChannel(kClient, remote)
    ChannelMgr-->>MsgHandler: channel

    alt channel == nullptr
        MsgHandler-->>Engine: NOT_CONNECTED
        Engine-->>Client: NOT_CONNECTED
    else
        MsgHandler->>MsgHandler: ParseListenInfo(remote)
        MsgHandler->>Channel: StopHeartbeat()

        Note over MsgHandler: 通过插件建立TCP
        MsgHandler->>Plugin: Connect(remote_ip, remote_port, timeout)
        Plugin-->>MsgHandler: conn_fd

        Note over MsgHandler: 发送断链消息
        MsgHandler->>Plugin: SendMsg(conn_fd, kDisconnect, disconnect_info)

        Note over MsgHandler: 本地销毁Client通道
        MsgHandler->>MsgHandler: DisconnectInfoProcess(kClient,...)
        MsgHandler->>ChannelMgr: DestroyChannel(kClient, remote)

        Note over MsgHandler: 接收对端处理状态
        MsgHandler->>Plugin: RecvMsg(conn_fd, kStatus, status)

        alt status.error_code != SUCCESS
            MsgHandler-->>Engine: status.error_code
            Engine-->>Client: status.error_code
        else
            MsgHandler-->>Engine: SUCCESS
            Engine-->>Client: SUCCESS
        end
    end

    deactivate MsgHandler
    deactivate Engine
```

## 守护与被动处理（StartDaemon / ConnectedProcess）

```mermaid
sequenceDiagram
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant Channel as Channel

    Engine->>MsgHandler: Initialize(options,...)
    MsgHandler->>Plugin: StartDaemon(listen_port)
    Plugin-->>MsgHandler: started

    Plugin->>MsgHandler: ConnectedProcess(fd)
    alt msg == kConnect
        MsgHandler->>MsgHandler: ProcessConnectRequest(fd, msg)
        MsgHandler->>ChannelMgr: CreateChannel(...Server)
        MsgHandler->>Channel: SetSocketNonBlocking(fd)
        MsgHandler->>ChannelMgr: AddSocketToEpoll(fd, channel)
        MsgHandler->>Plugin: SendMsg(fd, kStatus, SUCCESS)
    else msg == kDisconnect
        MsgHandler->>MsgHandler: ProcessDisconnectRequest(fd, msg)
        MsgHandler->>ChannelMgr: DestroyChannel(kServer, id)
        MsgHandler->>Plugin: SendMsg(fd, kStatus, SUCCESS)
    end
```

## 关键组件说明

### AdxlInnerEngine

- 对外提供 `Connect` 和 `Disconnect` 接口
- 内部委托给 `ChannelMsgHandler` 处理

### ChannelMsgHandler
- 负责TCP连接管理和消息交换
- `Connect`: 建立TCP连接，交换连接信息，创建Channel
- `Disconnect`: 发送断开消息，销毁Channel

### ChannelManager
- 管理所有Channel的生命周期
- 提供Channel的创建、获取、销毁功能
- 管理epoll事件和心跳机制

### Channel
- 封装HCCL通信资源
- 提供数据传输功能
- 管理socket连接和心跳

### SegmentTable
- 记录本地和远程内存段信息
- 用于判断传输类型（H2H, H2D, D2H, D2D）

## 连接建立的关键步骤

1. **TCP连接建立**: 客户端通过TCP连接到远程引擎
2. **信息交换**: 双方交换 `ChannelConnectInfo`（包含channel_id, comm_res, addrs等）
3. **Rank Table生成**: 根据comm_res生成rank table，确定local_rank和peer_rank
4. **Channel创建**: 创建Channel对象并初始化HCCL通信资源
5. **地址注册**: 将远程内存地址信息添加到SegmentTable
6. **Epoll注册**: 将socket添加到epoll，用于后续消息接收

## 断开连接的关键步骤

1. **停止心跳**: 停止向远程引擎发送心跳消息
2. **TCP重连**: 重新建立TCP连接用于发送断开消息
3. **消息通知**: 发送 `ChannelDisconnectInfo` 通知远程引擎
4. **资源清理**: 
   - 从epoll移除socket
   - 销毁Channel并清理HCCL资源
   - 从ChannelManager中移除Channel
5. **状态确认**: 接收远程引擎的状态确认消息

---

## 引入 Expire 机制后的 Connect 流程时序图

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant ExpireThread as ExpireThread
    participant Channel as Channel
    participant SegmentTable as SegmentTable

    Client->>Engine: Connect(remote_engine, timeout)
    activate Engine
    Engine->>MsgHandler: Connect(remote_engine, timeout)
    activate MsgHandler
    
    Note over MsgHandler: 检查channel是否已存在
    MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
    ChannelMgr-->>MsgHandler: nullptr (不存在)
    
    Note over MsgHandler: 检查水位线
    MsgHandler->>ChannelMgr: GetAllClientChannel()
    ChannelMgr-->>MsgHandler: client_channels
    MsgHandler->>ChannelMgr: GetAllServerChannel()
    ChannelMgr-->>MsgHandler: server_channels
    
    alt 达到高水位 (total_channels >= max_channel * waterline_threshold)
        Note over MsgHandler: 触发淘汰流程
        MsgHandler->>MsgHandler: CheckWaterlineAndExpire()
        activate ExpireThread
        Note over ExpireThread: 选择淘汰候选通道
        ExpireThread->>ExpireThread: SelectExpireCandidates()
        Note over ExpireThread: 比较Client和Server数量
        Note over ExpireThread: 按活跃标识和建链顺序排序
        Note over ExpireThread: 执行淘汰
        loop 对每个候选通道
            ExpireThread->>ExpireThread: ExpireChannel(channel)
            Note over ExpireThread: 如果是Server通道，查询Client状态
            alt channel_type == kServer
                ExpireThread->>MsgHandler: QueryTransferStatus(channel, timeout)
                activate MsgHandler
                MsgHandler->>Channel: SendControlMsg(kTransferStatusQuery)
                Channel->>Remote: 通过socket发送查询请求
                Remote->>Remote: HandleTransferStatusQuery()
                Remote->>Remote: GetTransferStatus()
                Remote->>Channel: SendControlMsg(kTransferStatusResp)
                Channel->>MsgHandler: 接收响应
                MsgHandler-->>ExpireThread: TransferStatusResp
                deactivate MsgHandler
                alt resp.can_disconnect == false
                    Note over ExpireThread: 跳过此通道，不可淘汰
                    ExpireThread->>ExpireThread: continue
                end
            end
            ExpireThread->>MsgHandler: Disconnect(channel_id, timeout)
            MsgHandler->>MsgHandler: 执行断链流程
        end
        Note over ExpireThread: 等待水位下降到目标水位
        ExpireThread->>ExpireThread: WaitUntilWaterlineReached()
        deactivate ExpireThread
    end
    
    Note over MsgHandler: 解析远程引擎信息
    MsgHandler->>MsgHandler: ParseListenInfo(remote_engine)

    Note over MsgHandler: 通过插件建立TCP并交换连接信息
    MsgHandler->>Plugin: Connect(remote_ip, remote_port, timeout)
    Plugin-->>MsgHandler: conn_fd
    MsgHandler->>MsgHandler: 构造ChannelConnectInfo
    MsgHandler->>Plugin: SendMsg(conn_fd, kConnect, connect_info)
    MsgHandler->>Plugin: RecvMsg(conn_fd, kConnect, peer_connect_info)
    
    Note over MsgHandler: 处理连接信息
    MsgHandler->>MsgHandler: ConnectInfoProcess(peer_connect_info, timeout, true)
    
    Note over MsgHandler: 生成rank table
    MsgHandler->>MsgHandler: RankTableGenerator::Generate()
    MsgHandler->>MsgHandler: 获取local_rank_id和peer_rank_id
    
    Note over MsgHandler: 构造ChannelInfo
    MsgHandler->>MsgHandler: 构造ChannelInfo
    
    Note over MsgHandler: 创建Channel
    MsgHandler->>ChannelMgr: CreateChannel(channel_info, channel_ptr)
    activate ChannelMgr
    ChannelMgr->>Channel: new Channel(channel_info)
    ChannelMgr->>Channel: Initialize()
    activate Channel
    Channel->>Channel: 初始化HCCL通信资源
    Channel-->>ChannelMgr: SUCCESS
    deactivate Channel
    ChannelMgr->>ChannelMgr: 添加到channels_映射
    ChannelMgr-->>MsgHandler: channel_ptr
    deactivate ChannelMgr
    
    Note over MsgHandler: 添加远程地址到SegmentTable
    MsgHandler->>SegmentTable: AddRange(channel_id, start_addr, end_addr, mem_type)
    
    Note over MsgHandler: 接收远端处理状态
    MsgHandler->>Plugin: RecvMsg(conn_fd, kStatus, status)
    
    alt status.error_code != SUCCESS
        MsgHandler-->>Engine: FAILED
        Engine-->>Client: FAILED
    else status.error_code == SUCCESS
        Note over MsgHandler: 获取创建的channel
        MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
        ChannelMgr-->>MsgHandler: channel
        
        Note over MsgHandler: 设置socket并添加到epoll
        MsgHandler->>Channel: SetSocketNonBlocking(conn_fd)
        MsgHandler->>ChannelMgr: AddSocketToEpoll(conn_fd, channel)
        
        MsgHandler-->>Engine: SUCCESS
        Engine-->>Client: SUCCESS
    end
    
    deactivate MsgHandler
    deactivate Engine
```

## Expire 淘汰流程详细时序图

```mermaid
sequenceDiagram
    participant ExpireThread as ExpireThread
    participant MsgHandler as ChannelMsgHandler
    participant ChannelMgr as ChannelManager
    participant ClientChannel as Client Channel
    participant ServerChannel as Server Channel
    participant RemoteClient as Remote Client

    Note over ExpireThread: 淘汰线程被触发（达到高水位）
    ExpireThread->>ChannelMgr: GetAllClientChannel()
    ChannelMgr-->>ExpireThread: client_channels[]
    ExpireThread->>ChannelMgr: GetAllServerChannel()
    ChannelMgr-->>ExpireThread: server_channels[]
    
    Note over ExpireThread: 选择淘汰目标
    ExpireThread->>ExpireThread: SelectExpireTarget()
    alt client_channels.size() >= server_channels.size()
        Note over ExpireThread: 淘汰Client通道
        ExpireThread->>ExpireThread: expire_channels = client_channels
    else
        Note over ExpireThread: 淘汰Server通道
        ExpireThread->>ExpireThread: expire_channels = server_channels
    end
    
    Note over ExpireThread: 按淘汰策略排序
    ExpireThread->>ExpireThread: SortByExpirePolicy()
    Note over ExpireThread: 1. 优先淘汰 has_transferred == false
    Note over ExpireThread: 2. 按建链顺序（创建时间）
    
    Note over ExpireThread: 计算需要淘汰的数量
    ExpireThread->>ExpireThread: need_expire = total_channels - (max_channel * waterline_target)
    
    loop 对每个候选通道（直到达到目标水位）
        ExpireThread->>ExpireThread: channel = expire_channels[i]
        
        Note over ExpireThread: 检查通道状态
        ExpireThread->>ExpireThread: CheckChannelState(channel)
        alt channel.transfer_count > 0 || channel.disconnect_flag == true
            Note over ExpireThread: 跳过，通道正在使用
            ExpireThread->>ExpireThread: continue
        end
        
        alt channel.channel_type == kServer
            Note over ExpireThread: Server通道需要查询Client状态
            ExpireThread->>MsgHandler: QueryTransferStatus(channel, timeout)
            activate MsgHandler
            MsgHandler->>ServerChannel: SendControlMsg(kTransferStatusQuery)
            activate ServerChannel
            ServerChannel->>RemoteClient: 通过socket发送查询请求
            activate RemoteClient
            RemoteClient->>RemoteClient: HandleTransferStatusQuery()
            RemoteClient->>RemoteClient: GetTransferStatus()
            Note over RemoteClient: 读取 transfer_count, has_transferred
            RemoteClient->>RemoteClient: can_disconnect = (transfer_count == 0 && !has_transferred)
            RemoteClient->>ServerChannel: SendControlMsg(kTransferStatusResp)
            deactivate RemoteClient
            ServerChannel->>MsgHandler: 接收响应
            MsgHandler->>MsgHandler: ProcessTransferStatusResp()
            MsgHandler-->>ExpireThread: TransferStatusResp
            deactivate ServerChannel
            deactivate MsgHandler
            
            alt resp.can_disconnect == false
                Note over ExpireThread: Client端有传输任务，跳过
                ExpireThread->>ExpireThread: continue
            end
        end
        
        Note over ExpireThread: 执行淘汰
        ExpireThread->>MsgHandler: Disconnect(channel.channel_id, timeout)
        activate MsgHandler
        MsgHandler->>MsgHandler: 执行标准断链流程
        Note over MsgHandler: 1. 停止心跳
        Note over MsgHandler: 2. 发送断链消息
        Note over MsgHandler: 3. 销毁Channel
        MsgHandler-->>ExpireThread: SUCCESS
        deactivate MsgHandler
        
        Note over ExpireThread: 更新淘汰计数
        ExpireThread->>ExpireThread: expired_count++
        
        alt expired_count >= need_expire
            Note over ExpireThread: 已达到目标水位
            ExpireThread->>ExpireThread: break
        end
    end
    
    Note over ExpireThread: 重置所有通道的活跃标识
    ExpireThread->>ChannelMgr: ResetAllTransferFlags()
    ChannelMgr->>ChannelMgr: 遍历所有通道
    loop 对每个通道
        ChannelMgr->>ClientChannel: SetTransferFlag(false)
        ChannelMgr->>ServerChannel: SetTransferFlag(false)
    end
    ChannelMgr-->>ExpireThread: SUCCESS
    
    Note over ExpireThread: 淘汰完成，等待下次触发
```

## Server 端查询 Client 端传输状态时序图

```mermaid
sequenceDiagram
    participant Server as Server端
    participant ServerChannel as Server Channel
    participant Socket as Socket连接
    participant ClientChannel as Client Channel
    participant Client as Client端

    Note over Server: Server端需要淘汰Server Channel
    Server->>ServerChannel: QueryTransferStatus(timeout)
    activate ServerChannel
    
    Note over ServerChannel: 生成查询ID
    ServerChannel->>ServerChannel: query_id = next_query_id_++
    
    Note over ServerChannel: 构造查询请求
    ServerChannel->>ServerChannel: TransferStatusQuery{query_id, timeout}
    
    Note over ServerChannel: 注册等待响应
    ServerChannel->>ServerChannel: pending_resps_[query_id] = {resp, cv}
    
    Note over ServerChannel: 发送查询请求
    ServerChannel->>Socket: SendControlMsg(kTransferStatusQuery, query)
    activate Socket
    Socket->>ClientChannel: 通过socket传输
    activate ClientChannel
    
    Note over ClientChannel: 接收查询请求
    ClientChannel->>ClientChannel: HandleTransferStatusQuery(query)
    
    Note over ClientChannel: 获取传输状态
    ClientChannel->>ClientChannel: GetTransferStatus()
    Note over ClientChannel: 读取 transfer_count_
    Note over ClientChannel: 读取 has_transferred_
    Note over ClientChannel: can_disconnect = (transfer_count_ == 0 && !has_transferred_)
    
    Note over ClientChannel: 构造响应
    ClientChannel->>ClientChannel: TransferStatusResp{query_id, transfer_count, has_transferred, can_disconnect}
    
    Note over ClientChannel: 发送响应
    ClientChannel->>Socket: SendControlMsg(kTransferStatusResp, resp)
    deactivate ClientChannel
    Socket->>ServerChannel: 通过socket传输响应
    deactivate Socket
    
    Note over ServerChannel: 接收响应
    ServerChannel->>ServerChannel: ProcessTransferStatusResp(resp)
    
    Note over ServerChannel: 查找等待槽
    ServerChannel->>ServerChannel: pending_resps_.find(query_id)
    
    Note over ServerChannel: 填充响应并唤醒
    ServerChannel->>ServerChannel: pending_resps_[query_id].first = resp
    ServerChannel->>ServerChannel: pending_resps_[query_id].second.notify_one()
    
    Note over ServerChannel: 等待线程被唤醒
    ServerChannel->>ServerChannel: 读取响应
    ServerChannel->>ServerChannel: pending_resps_.erase(query_id)
    
    ServerChannel-->>Server: TransferStatusResp{can_disconnect, ...}
    deactivate ServerChannel
    
    alt resp.can_disconnect == true
        Note over Server: 可以淘汰，执行断链
        Server->>Server: Disconnect(channel_id)
    else
        Note over Server: 不可淘汰，跳过此通道
        Server->>Server: continue
    end
```

## 传输状态更新时序图（TransferSync 时）

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant Channel as Channel
    participant BTS as BufferTransferService

    Client->>Engine: TransferSync(remote_engine, op, op_descs, timeout)
    activate Engine
    Engine->>Channel: TransferSync(op, op_descs, timeout)
    activate Channel
    
    Note over Channel: 开始传输
    Channel->>Channel: transfer_count_++
    Channel->>Channel: has_transferred_ = true
    Channel->>Channel: last_active_time_ = now()
    
    alt 需要buffer中转
        Channel->>BTS: Transfer(channel, type, op_descs, timeout)
        activate BTS
        BTS->>BTS: DoTransferTask()
        Note over BTS: 执行数据传输
        BTS-->>Channel: SUCCESS
        deactivate BTS
    else 直接传输
        Channel->>Channel: D2DTransfer(op, op_descs)
        Note over Channel: 执行HCCL传输
    end
    
    Note over Channel: 传输完成
    Channel->>Channel: transfer_count_--
    Channel->>Channel: last_active_time_ = now()
    
    Channel-->>Engine: SUCCESS
    Engine-->>Client: SUCCESS
    deactivate Channel
    deactivate Engine
```

## Expire 机制关键组件说明

### ChannelMsgHandler 新增功能

- **水位线管理**: 
  - `max_channel`: 最大通道数上限
  - `waterline_threshold`: 高水位阈值（如 0.9，表示达到 max_channel * 0.9 时触发淘汰）
  - `waterline_target`: 目标水位（如 0.8，表示淘汰到 max_channel * 0.8）

- **淘汰线程**: 
  - `ExpireThread`: 独立线程，监听水位线并执行淘汰
  - `CheckWaterlineAndExpire()`: 检查水位并触发淘汰
  - `SelectExpireCandidates()`: 选择淘汰候选通道
  - `QueryTransferStatus()`: 查询对端传输状态（用于Server通道）

### Channel 新增状态字段

- `transfer_count_`: 原子变量，当前在途传输任务数
- `disconnect_flag_`: 原子变量，是否正在断链
- `has_transferred_`: bool变量，本轮是否发生过数据传输
- `last_active_time_`: 最近活跃时间戳

### 淘汰策略

1. **选择淘汰目标**: 
   - 比较Client和Server通道数量
   - 谁多淘汰谁，一样多优先淘汰Client

2. **排序规则**:
   - 第一优先级: `has_transferred == false` 的通道优先
   - 第二优先级: 按建链顺序（创建时间）排序

3. **保护机制**:
   - `transfer_count > 0`: 正在传输，不可淘汰
   - `disconnect_flag == true`: 正在断链，跳过
   - Server通道需查询Client状态，`can_disconnect == false` 时跳过

4. **状态重置**:
   - 每轮淘汰完成后，所有通道的 `has_transferred` 重置为 `false`
   - 开始新一轮的活跃度追踪

