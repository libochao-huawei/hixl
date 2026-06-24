# API Reference (C++)

## Overview

- LLM-DataDist
  - `LLM-DataDist` APIs are stored in `INSTALL_DIR/include/llm_datadist/llm_datadist.h`. Replace `INSTALL_DIR` with the actual file storage path after the CANN software is installed. For example, if the `Ascend-CANN-Toolkit` software package is installed as the `root` user, the file storage path after the installation is `/usr/local/Ascend/cann`.
  - The library file corresponding to `LLM-DataDist `APIs is `libllm_datadist.so`.

- HIXL: Huawei Xfer Library
  - `HIXL` APIs are stored in `INSTALL_DIR/include/hixl/hixl.h`. Replace `INSTALL_DIR` with the actual file storage path after the CANN software is installed. For example, if the `Ascend-CANN-Toolkit` software package is installed as the `root` user, the file storage path after the installation is `/usr/local/Ascend/cann`.
  - The library file corresponding to `HIXL` APIs is `libcann_hixl.so`.
  - `HIXL_CS` APIs are stored in `INSTALL_DIR/include/cs/hixl_cs.h`. Replace `INSTALL_DIR` with the actual file storage path after the CANN software is installed. For example, if the `Ascend-CANN-Toolkit` software package is installed as the `root` user, the file storage path after the installation is `/usr/local/Ascend/cann`. Only Ascend 950PR and Ascend 950DT are supported.
  - The library file corresponding to `HIXL_CS` APIs is `libcann_hixl.so`. Only Ascend 950PR and Ascend 950DT are supported.

The following products are supported:

- Atlas A2 training products/Atlas A2 inference products: Only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported. When the HCCS transmission protocol is used in the server, `LLM-DataDist` APIs support only D2D transmission.
- Atlas A3 training products/Atlas A3 inference products: When the HCCS transmission protocol is used, `LLM-DataDist` APIs do not support the host memory as the remote cache.
- Ascend 950PR/Ascend 950DT: The UB protocol is used within a SuperPoD, and the RoCE protocol is used between SuperPoDs.

## APIs

The API list is as follows.

- [LLM-DataDist APIs](LLM-DataDist APIs.md)
- [LLM-DataDist Data Structures](LLM-DataDist Data Structures.md)
- [LLM-DataDist Error Codes](LLM-DataDist Error Codes.md)
- [HIXL_CS APIs](HIXL_CS APIs.md)
- [HIXL APIs](HIXL APIs.md)
- [HIXL Data Structures](HIXL Data Structures.md)
- [HIXL Error Codes](HIXL Error Codes.md)
- [Unsupported APIs](Unsupported APIs.md)
- [To Be Discarded_ADXL APIs](To Be Discarded_ADXL APIs.md)
- [To Be Discarded_ADXL Data Structures](To Be Discarded_ADXL Data Structures.md)
- [To Be Discarded_ADXL Error Codes](To Be Discarded_ADXL Error Codes.md)
