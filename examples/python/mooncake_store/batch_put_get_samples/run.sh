export ASCEND_GLOBAL_EVENT_ENABLE=1
export ASCEND_HOST_LOG_FILE_NUM=500
# export HCCL_INTRA_PCIE_ENABLE=1

# 默认走hccs，可以通过下面参数调整为roce
# export HCCL_INTRA_ROCE_ENABLE=1

source /usr/local/Ascend/ascend-toolkit/set_env.sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib64/python3.11/site-packages/mooncake/

# mooncake参数
export ASCEND_BUFFER_POOL=4:8 # adxl环境变量 BUFFER_NUM:BUFFER_SIZE (MB)
export MC_ALLOC_SAME_NODE_FIRST=1 # 内存分配策略，优先在同一个NUMA节点上分配

python3 $@