# iMX Smart Voice

基于 NXP i.MX6ULL 开发板的智能语音助手系统，支持语音交互、图像识别与图形界面，采用客户端-服务器架构，融合多种嵌入式与 AI 技术。

## 功能概览

- **一键语音交互**：按键触发录音，ASR 转文字，LLM 生成回复，TTS 合成语音并播放。
- **图像识别**：摄像头采集图像，结合大模型进行识别。
- **嵌入式 GUI**：基于 Qt 显示对话状态与历史记录。
- **本地存储**：使用 SQLite 持久化对话记录。
- **通信机制**：支持 Socket 网络传输与共享内存等本地进程间通信。
- **软硬件集成**：集成音频（ALSA）、视频（V4L2）、Wi-Fi、触控屏等设备驱动。

## 系统架构

### 客户端（i.MX6ULL）

- 控制硬件设备（按键、麦克风、摄像头、LCD 等）
- 采集音频（ALSA）与图像（V4L2 + JpegLib）
- 通过 Socket 与服务器通信
- 使用共享内存与信号量与 Qt GUI 同步数据
- 本地使用 SQLite 存储历史数据

### 服务器（Python + Flask）

- 接收音频并调用 Faster Whisper 进行语音识别（ASR）
- 利用 LLM API 生成响应文本
- 使用 ChatTTS 合成语音并发送回客户端
- 支持图像识别等视觉任务处理

## 技术栈

- **嵌入式开发**：C/C++、ALSA、V4L2、Qt5、SQLite、Buildroot/Yocto
- **服务器端**：Python、Flask、Faster Whisper、ChatTTS、LLM API（如 OpenAI、文心一言）
- **通信机制**：Socket、共享内存、信号量

## 硬件要求

- NXP i.MX6ULL 开发板（带 LCD、摄像头、Wi-Fi）
- 麦克风模块、扬声器模块
- GT1151 触控屏、OV5640 摄像头、8188EU Wi-Fi 模块

## 软件要求

### 客户端

- 交叉编译工具链
- 嵌入式 Linux（Buildroot/Yocto）
- 所需依赖库：ALSA、V4L2、JpegLib、SQLite3、Qt5 等

### 服务器

- Python 3.10+
- 所需库：`flask`、`faster-whisper`、`ChatTTS`、`requests`、`numpy` 等
- （可选）GPU 加速环境以优化模型推理性能