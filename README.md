# iMX Smart Voice

本项目基于 NXP I.MX6ULL 开发板构建了一套智能语音助手系统。核心功能包括：通过硬件按键触发语音录制，将录制的语音数据发送至后端服务进行高精度语音转文本（ASR），然后将文本输入大模型（LLM）API 进行智能问答或处理，并将大模型返回的文本结果通过文本转语音（TTS）服务转换为音频，最终通过扬声器播放。此外，系统还集成了摄像头功能，支持采集图像并通过大模型进行物品识别等视觉智能应用。整个系统集成了嵌入式Linux驱动开发、音视频处理、网络通信、跨进程通信、图形界面以及AI模型服务调用等多个技术栈。

## 主要特性 (Key Features)

*   **按键触发录音与控制:** 通过 GPIO 按键便捷地控制语音录制的开始与结束。
*   **高精度语音转文本 (ASR):** 利用部署在后端的 Faster Whisper 模型，将录制的语音内容转换为准确的文字。
*   **大模型智能交互:** 调用外部 LLM API，对文本输入进行理解、处理并生成智能响应。
*   **高质量文本转语音 (TTS):** 使用部署在后端的 ChatTTS 模型，将大模型返回的文本结果合成为自然流畅的语音。
*   **音视频设备集成:** 深度集成 ALSA 音频框架实现麦克风录音和扬声器播放；集成 V4L2 框架实现摄像头图像采集。
*   **摄像头视觉识别:** 支持通过 OV5640 摄像头采集图像，并可将图像发送至后端结合大模型进行物品识别等应用。
*   **嵌入式图形界面 (GUI):** 基于 QT 开发用户界面，实时显示录音状态、识别进度、对话内容及历史记录，提升用户体验。
*   **本地历史会话存储:** 使用 SQLite 数据库在设备端持久化存储历史对话记录。
*   **客户端/服务器架构:** 嵌入式开发板作为客户端负责硬件交互、数据采集和界面显示；独立后端服务器负责计算密集型的 ASR/TTS 模型推理及 LLM API 调用。
*   **跨进程通信:** 利用 Socket 实现客户端与后端服务间的音视频/文本数据传输；使用共享内存和信号量实现客户端核心逻辑与 QT 图形界面间的数据同步与通信。
*   **网络连接:** 支持 8188EU Wi-Fi 模块，实现开发板的网络连接能力。

## 系统架构 (System Architecture)
系统采用客户端/服务器架构：
1.  **i.MX6ULL 开发板 (客户端):**
    *   负责硬件驱动与外设控制 (按键, LCD, Wi-Fi, 摄像头, 音频)。
    *   通过 ALSA 采集麦克风音频。
    *   通过 V4L2 采集摄像头图像，使用 JpegLib 处理。
    *   通过 Socket 与后端服务器进行音频/图像上传及文本数据交换。
    *   通过共享内存和信号量与本地 QT GUI 进行数据通信。
    *   使用 SQLite 存储历史对话。
    *   播放后端返回的 TTS 音频。
    *   核心逻辑使用 C/C++ 实现。
2.  **后端服务器 (Python/Flask):**
    *   基于 Flask 构建 RESTful API 服务。
    *   接收客户端上传的音频数据，调用 Faster Whisper 进行 ASR。
    *   将 ASR 结果及相关指令发送至 LLM API 获取响应。
    *   接收 LLM 响应文本，调用 ChatTTS 进行 TTS。
    *   将 TTS 音频数据发送回客户端。
    *   接收客户端上传的图像数据，结合大模型进行视觉处理（如物品识别）。

## 关键技术栈 (Key Technologies Used)
*   **嵌入式系统:** I.MX6ULL, Buildroot/Yocto (或其他嵌入式 Linux 构建系统)
*   **硬件驱动:** ALSA, V4L2, GPIO, LCD (GT1151), Wi-Fi (8188EU), Camera (OV5640)
*   **编程语言:** C/C++, Python
*   **通信:** Socket (TCP/IP), Shared Memory, Semaphores
*   **图形界面:** QT5 (Embedded)
*   **数据库:** SQLite3
*   **音频处理:** ALSA 库
*   **图像处理:** V4L2 框架, JpegLib
*   **后端框架:** Python Flask
*   **AI 模型:** Faster Whisper (ASR), ChatTTS (TTS), LLM API (如 OpenAI, Wenxin Yiyan 等)

## 硬件要求 (Hardware Requirements)
*   NXP I.MX6ULL 开发板 (推荐带有 LCD, 摄像头, Wi-Fi 接口)
*   麦克风模块
*   扬声器模块
*   连接到开发板 GPIO 的按键
*   GT1151 LCD 触摸屏 (用于 GUI 显示和交互)
*   OV5640 摄像头模块 (用于视觉识别功能)
*   8188EU Wi-Fi 模块 (用于网络连接)
## 软件要求 (Software Requirements)
*   **开发环境:**
    *   交叉编译工具链 (适用于 I.MX6ULL)
    *   嵌入式 Linux 文件系统 (Buildroot, Yocto 或其他)
    *   必要的库和头文件 (ALSA, V4L2, JpegLib, SQLite3, QT5, Socket, IPC)
*   **后端服务器环境:**
    *   Python 3.10+
    *   Pip 包管理器
    *   所需的 Python 库: `flask`, `faster-whisper`, `ChatTTS`, `requests`, `numpy` 等
    *   (可选) GPU 环境以加速 ASR/TTS 模型推理