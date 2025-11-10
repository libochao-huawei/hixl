# AdxlInnerEngine Connect 和 Disconnect 时序图

## Connect 流程时序图

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant ChannelMgr as ChannelManager
    participant Channel as Channel
    participant Remote as 远程引擎
    participant SegmentTable as SegmentTable

    Client->>Engine: Connect(remote_engine, timeout)
    activate Engine
    Engine->>MsgHandler: Connect(remote_engine, timeout)
    activate MsgHandler
    
    Note over MsgHandler: 检查channel是否已存在
    MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
    ChannelMgr-->>MsgHandler: nullptr (不存在)
    
    Note over MsgHandler: 解析远程引擎信息
    MsgHandler->>MsgHandler: ParseListenInfo(remote_engine)
    
    Note over MsgHandler: TCP连接远程引擎
    MsgHandler->>Remote: TCP Connect(remote_ip, remote_port)
    activate Remote
    Remote-->>MsgHandler: conn_fd
    
    Note over MsgHandler: 发送本地连接信息
    MsgHandler->>MsgHandler: 构造ChannelConnectInfo
    MsgHandler->>Remote: SendMsg(kConnect, connect_info)
    
    Note over Remote: 接收连接请求
    Remote->>Remote: ProcessConnectRequest(fd, msg)
    Remote->>Remote: 构造并发送本地连接信息
    Remote->>MsgHandler: SendMsg(kConnect, peer_connect_info)
    
    Note over MsgHandler: 接收远程连接信息
    MsgHandler->>Remote: RecvMsg(kConnect, peer_connect_info)
    Remote-->>MsgHandler: peer_connect_info
    
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
    
    Note over Remote: 处理连接信息
    Remote->>Remote: ConnectInfoProcess(peer_connect_info, timeout, false)
    Remote->>Remote: CreateChannel(channel_info, false, remote_addrs)
    Remote->>Remote: 创建Server Channel
    
    Note over Remote: 设置socket并添加到epoll
    Remote->>Remote: SetSocketNonBlocking(fd)
    Remote->>Remote: AddSocketToEpoll(fd, channel)
    
    Note over Remote: 发送状态消息
    Remote->>MsgHandler: SendMsg(kStatus, status)
    
    Note over MsgHandler: 接收状态消息
    MsgHandler->>Remote: RecvMsg(kStatus, status)
    Remote-->>MsgHandler: status
    
    alt status.error_code != SUCCESS
        MsgHandler-->>Engine: FAILED
        Engine-->>Client: FAILED
    else status.error_code == SUCCESS
        Note over MsgHandler: 获取创建的channel
        MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
        ChannelMgr-->>MsgHandler: channel
        
        Note over MsgHandler: 设置socket并添加到epoll
        MsgHandler->>Channel: SetSocketNonBlocking(conn_fd)
        activate Channel
        Channel-->>MsgHandler: SUCCESS
        deactivate Channel
        MsgHandler->>ChannelMgr: AddSocketToEpoll(conn_fd, channel)
        ChannelMgr-->>MsgHandler: SUCCESS
        
        MsgHandler-->>Engine: SUCCESS
        Engine-->>Client: SUCCESS
    end
    
    deactivate Remote
    deactivate MsgHandler
    deactivate Engine
```

## Disconnect 流程时序图

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant ChannelMgr as ChannelManager
    participant Channel as Channel
    participant Remote as 远程引擎

    Client->>Engine: Disconnect(remote_engine, timeout)
    activate Engine
    Engine->>MsgHandler: Disconnect(remote_engine, timeout)
    activate MsgHandler
    
    Note over MsgHandler: 解析远程引擎信息
    MsgHandler->>MsgHandler: ParseListenInfo(remote_engine)
    
    Note over MsgHandler: 获取channel
    MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
    ChannelMgr-->>MsgHandler: channel
    
    alt channel == nullptr
        MsgHandler-->>Engine: NOT_CONNECTED
        Engine-->>Client: NOT_CONNECTED
    else channel != nullptr
        Note over MsgHandler: 停止心跳
        MsgHandler->>Channel: StopHeartbeat()
        activate Channel
        Channel->>Channel: 设置disconnect_flag_ = true
        Channel-->>MsgHandler: 
        deactivate Channel
        
        Note over MsgHandler: TCP连接远程引擎
        MsgHandler->>Remote: TCP Connect(remote_ip, remote_port)
        activate Remote
        Remote-->>MsgHandler: conn_fd
        
        Note over MsgHandler: 发送断开连接消息
        MsgHandler->>MsgHandler: 构造ChannelDisconnectInfo
        MsgHandler->>Remote: SendMsg(kDisconnect, disconnect_info)
        
        Note over MsgHandler: 处理本地断开信息
        MsgHandler->>MsgHandler: DisconnectInfoProcess(kClient, local_disconnect_info)
        MsgHandler->>ChannelMgr: DestroyChannel(kClient, remote_engine)
        activate ChannelMgr
        
        Note over ChannelMgr: 销毁Channel
        ChannelMgr->>ChannelMgr: RemoveFd(channel->GetFd())
        ChannelMgr->>ChannelMgr: 从epoll移除fd
        ChannelMgr->>Channel: Finalize()
        activate Channel
        Channel->>Channel: 清理HCCL通信资源
        Channel-->>ChannelMgr: SUCCESS
        deactivate Channel
        ChannelMgr->>ChannelMgr: 从channels_映射中移除
        ChannelMgr-->>MsgHandler: SUCCESS
        deactivate ChannelMgr
        
        Note over Remote: 处理断开请求
        Remote->>Remote: ProcessDisconnectRequest(fd, msg)
        Remote->>Remote: DisconnectInfoProcess(kServer, peer_disconnect_info)
        Remote->>Remote: DestroyChannel(kServer, channel_id)
        Remote->>Remote: 清理Server Channel资源
        
        Note over Remote: 发送状态消息
        Remote->>MsgHandler: SendMsg(kStatus, status)
        
        Note over MsgHandler: 接收状态消息
        MsgHandler->>Remote: RecvMsg(kStatus, status)
        Remote-->>MsgHandler: status
        
        alt status.error_code != SUCCESS
            MsgHandler-->>Engine: status.error_code
            Engine-->>Client: status.error_code
        else status.error_code == SUCCESS
            MsgHandler-->>Engine: SUCCESS
            Engine-->>Client: SUCCESS
        end
    end
    
    deactivate MsgHandler
    deactivate Engine
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

## 引入 Expire 机制后的 Connect 流程时序图（含 MsgHandlerPlugin）

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant Engine as AdxlInnerEngine
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant Channel as Channel
    participant Remote as 远程引擎
    participant SegmentTable as SegmentTable

    Client->>Engine: Connect(remote_engine, timeout)
    activate Engine
    Engine->>MsgHandler: Connect(remote_engine, timeout)
    activate MsgHandler

    Note over MsgHandler: 检查channel是否已存在
    MsgHandler->>ChannelMgr: GetChannel(kClient, remote_engine)
    ChannelMgr-->>MsgHandler: nullptr (不存在)

    Note over MsgHandler: 检查水位线，必要时淘汰
    MsgHandler->>ChannelMgr: GetAllClientChannel() / GetAllServerChannel()
    ChannelMgr-->>MsgHandler: 列表
    alt total >= max_channel * waterline_threshold
        MsgHandler->>MsgHandler: CheckWaterlineAndExpire()
        Note over MsgHandler: 内部执行淘汰直至 waterline_target
    end

    Note over MsgHandler,Plugin: 通过 Plugin 建立底层连接
    MsgHandler->>Plugin: Connect(remote_ip, remote_port, timeout)
    Plugin-->>MsgHandler: conn_fd
    MsgHandler->>Remote: TCP 已建立（由 Plugin 提供 fd）
    activate Remote

    Note over MsgHandler,Plugin: 发送/接收握手消息
    MsgHandler->>MsgHandler: 构造ChannelConnectInfo
    MsgHandler->>Plugin: SendMsg(kConnect, connect_info)
    MsgHandler->>Plugin: RecvMsg(kConnect)
    Plugin-->>MsgHandler: peer_connect_info

    MsgHandler->>MsgHandler: ConnectInfoProcess(peer_connect_info, timeout, true)
    MsgHandler->>ChannelMgr: CreateChannel(channel_info, channel_ptr)
    activate ChannelMgr
    ChannelMgr->>Channel: new + Initialize()
    activate Channel
    Channel-->>ChannelMgr: SUCCESS
    deactivate Channel
    ChannelMgr-->>MsgHandler: channel_ptr
    deactivate ChannelMgr

    MsgHandler->>SegmentTable: AddRange(channel_id, start_addr, end_addr, mem_type)

    Note over Remote,Plugin: 远端处理并返回状态
    Remote->>Plugin: SendMsg(kStatus, status)
    MsgHandler->>Plugin: RecvMsg(kStatus)
    Plugin-->>MsgHandler: status

    alt status.error_code != SUCCESS
        MsgHandler-->>Engine: FAILED
        Engine-->>Client: FAILED
    else
        MsgHandler->>Channel: SetSocketNonBlocking(conn_fd)
        activate Channel
        Channel-->>MsgHandler: SUCCESS
        deactivate Channel
        MsgHandler->>ChannelMgr: AddSocketToEpoll(conn_fd, channel)
        ChannelMgr-->>MsgHandler: SUCCESS
        Note over MsgHandler,Plugin: 连接 fd 移交 Channel 管理
        MsgHandler-->>Engine: SUCCESS
        Engine-->>Client: SUCCESS
    end

    deactivate Remote
    deactivate MsgHandler
    deactivate Engine
```

## Expire 淘汰流程详细时序图（含 MsgHandlerPlugin 在断链阶段）

```mermaid
sequenceDiagram
    participant ExpireThread as ExpireThread
    participant MsgHandler as ChannelMsgHandler
    participant Plugin as MsgHandlerPlugin
    participant ChannelMgr as ChannelManager
    participant ClientChannel as Client Channel
    participant ServerChannel as Server Channel
    participant Remote as 远端对等方

    Note over ExpireThread: 达到高水位 → 触发淘汰
    ExpireThread->>ChannelMgr: GetAllClientChannel()/GetAllServerChannel()
    ChannelMgr-->>ExpireThread: 列表
    ExpireThread->>ExpireThread: 选择淘汰目标(谁多淘汰谁)
    ExpireThread->>ExpireThread: 按活跃标识(false优先) + 建链顺序排序
    ExpireThread->>ExpireThread: need = total - (max*waterline_target)

    loop 直到达到目标水位
        ExpireThread->>ExpireThread: 取下一个候选 channel
        alt transfer_count>0 或 disconnect_flag==true
            Note over ExpireThread: 跳过正在使用/已断链通道
            ExpireThread->>ExpireThread: continue
        end

        alt 候选是 Server 通道且需与 Client 确认
            MsgHandler->>ServerChannel: SendControlMsg(kTransferStatusQuery)
            ServerChannel->>Remote: 通过 socket 查询状态
            Remote-->>ServerChannel: kTransferStatusResp(can_disconnect,...)
            ServerChannel-->>MsgHandler: 回传响应
            alt can_disconnect==false
                Note over ExpireThread: 客户端忙，跳过
                ExpireThread->>ExpireThread: continue
            end
        end

        Note over MsgHandler,Plugin: 发起断链（协议级）
        ExpireThread->>MsgHandler: Disconnect(channel_id, timeout)
        MsgHandler->>Plugin: Connect(remote_ip, remote_port, timeout)
        Plugin-->>MsgHandler: conn_fd
        MsgHandler->>Plugin: SendMsg(kDisconnect, disconnect_info)
        MsgHandler->>Plugin: RecvMsg(kStatus)
        Plugin-->>MsgHandler: status
        MsgHandler->>ChannelMgr: DestroyChannel(type, channel_id)
        ChannelMgr-->>MsgHandler: SUCCESS
    end

    Note over ExpireThread: 重置 has_transferred = false（开始新回合）
```

## Server 端查询 Client 端传输状态时序图（控制消息路径说明）

```mermaid
sequenceDiagram
    participant Server as Server端
    participant ServerChannel as Server Channel
    participant Socket as Socket连接
    participant ClientChannel as Client Channel
    participant Client as Client端

    Note over Server: 淘汰 Server 通道前进行查询
    Server->>ServerChannel: QueryTransferStatus(timeout)

    Note over ServerChannel: 生成查询ID，注册等待槽
    ServerChannel->>Socket: SendControlMsg(kTransferStatusQuery, query)

    Socket->>ClientChannel: 传输查询请求
    ClientChannel->>ClientChannel: GetTransferStatus()
    ClientChannel->>Socket: SendControlMsg(kTransferStatusResp, resp)
    Socket->>ServerChannel: 传输状态响应

    ServerChannel->>ServerChannel: 唤醒等待线程，读取响应
    ServerChannel-->>Server: TransferStatusResp{can_disconnect,...}

    alt can_disconnect==true
        Server->>Server: 允许淘汰
    else
        Server->>Server: 跳过此通道
    end
```

