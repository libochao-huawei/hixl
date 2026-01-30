# 离线编译

离线编译是指在您的环境没有联网的情况下，对软件源码进行编译、并安装到目标服务器的过程。在本项目中，HIXL的编译过程依赖一些开源第三方软件，在离线状态下无法进行自动下载。

本文档提供了离线环境下的编译安装知道，在进行下面的操作之前，请确保已按照[环境准备](build.md#环境准备)完成了基础环境搭建。

## 源码下载

在离线环境中，由于无法通过`git`指令下载代码，须在联网环境中下载源码后，手动上传至目标环境。

- 在联网环境中，进入[本项目主页](https://gitcode.com/cann/hixl), 通过`下载ZIP`或`clone`按钮，根据指导，完成源码下载。
- 连接至离线环境中，上传源码至您指定的目录下。若下载的为源码压缩包，还需进行解压。

## 开源第三方软件下载 

HIXL在编译时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 | 下载地址 |
|---|---|---|
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz) |
| json | 3.11.3 | [include.zip](https://gitcode.com/cann-src-third-party/json/releases/download/v3.11.3/include.zip) |
| makeself | 2.5.0 | [makeself-release-2.5.0-patch1.tar.gz](https://gitcode.com/cann-src-third-party/makeself/releases/download/release-2.5.0-patch1.0/makeself-release-2.5.0-patch1.tar.gz) |
| pybind11 | 2.13.6 | [pybind11-2.13.6.tar.gz](https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz) |

> [!NOTE]说明
> 如果您从其他地址下载，请确保版本号一致。

在离线环境中进行编译时，您需要在联网环境中下载上述开源软件压缩包，并上传至离线环境中。

您需要新建一个`{your_3rd_party_path}`目录来存放这些第三方开源软件。按照如下目录结构创建文件夹后，将开源软件压缩包解压至对应文件夹中即可。

```
your_3rd_party_path/
|-- json    # json
|-- makeself    # makeself
|-- pybind11    # pybind11
|-- gtest   # googletest
```

> [!NOTE]说明
> - 在下载第三方开源软件压缩包并解压后，须修改文件夹名为上述目录结构中的对应名称。
> - 针对 `.zip` 结尾的压缩包，可使用 `unzip <file-name> -d /path/to/your/destination` 解压至您需要的目录。 
> - 针对 `.tar.gz` 结尾的压缩包，可使用 `tar -xzvf <file-name> -C /path/to/your/destination` 解压至您需要的目录。

## 编译

在离线环境中进行编译时，可通过`--cann_3rd_lib_path`参数来指定您的第三方开源软件目录，其默认值为`{hixl-project-path}/third_party`。

您可通过如下命令进行离线编译。

```
bash build.sh --cann_3rd_lib_path={your_3rd_party_path}
```

更多执行选项可参考[编译执行](./build.md#编译执行)章节中的说明，或通过命令`bash build.sh -h`进行查看。