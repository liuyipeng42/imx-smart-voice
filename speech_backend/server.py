from flask import Flask, request, jsonify
from faster_whisper import WhisperModel
import http.client
import numpy as np
import subprocess
import requests
import time
import os
import tts


def save_wav(file_path: str, sample_rate: int, audio_data: np.ndarray) -> bool:
    """保存为WAV文件"""
    try:
        from scipy.io.wavfile import write

        write(file_path, sample_rate, audio_data)
        return True
    except Exception as e:
        print(f"Failed to save WAV file: {e}")
        return False


def resample_wav(
    input_path: str, output_path: str, sample_rate: int = 44100, channels: int = 2
) -> bool:
    """
    使用FFmpeg重新采样WAV文件

    参数:
        input_path: 输入文件路径
        output_path: 输出文件路径
        sample_rate: 目标采样率(默认44100)
        channels: 目标声道数(默认2)

    返回:
        bool: 是否成功
    """
    try:
        # 检查输入文件是否存在
        if not os.path.exists(input_path):
            print(f"Input file not found: {input_path}")
            return False

        # 构建FFmpeg命令
        cmd = [
            "ffmpeg",
            "-y",  # 覆盖输出文件而不询问
            "-i",
            input_path,
            "-ar",
            str(sample_rate),
            "-ac",
            str(channels),
            output_path,
        ]

        # 执行命令
        subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return True
    except subprocess.CalledProcessError as e:
        print(f"FFmpeg command failed with return code {e.returncode}")
        print(f"Error output: {e.stderr.decode('utf-8') if e.stderr else 'None'}")
        return False
    except Exception as e:
        print(f"Error during resampling: {e}")
        return False


def send_audio_file(file_path, server_host="localhost", server_port=8001):
    try:
        # 验证文件存在性
        if not os.path.exists(file_path):
            print(f"错误：文件 {file_path} 不存在")
            return
        # 读取文件内容
        with open(file_path, "rb") as f:
            file_data = f.read()
            file_size = len(file_data)
        # 建立HTTP连接
        conn = http.client.HTTPConnection(server_host, server_port, timeout=10)

        # 设置请求头
        headers = {
            "Content-Type": "application/octet-stream",
            "Content-Length": str(file_size),
        }
        # 发送POST请求
        conn.request("POST", "/upload/audio", body=file_data, headers=headers)

        # 获取响应
        response = conn.getresponse()
        print(f"HTTP状态码: {response.status}")
        print(f"服务器响应: {response.read().decode()}")
    except Exception as e:
        print(f"发送失败: {str(e)}")
    finally:
        conn.close()


asr_model = WhisperModel(
    "./models/faster-whisper-large-v3", device="cuda", compute_type="int8_float16"
)

chattts_generator = tts.ChatTTSGenerator("./models/ChatTTS")

app = Flask(__name__)

client_ip = "10.33.103.61"

@app.route("/upload/audio", methods=["POST"])
def upload_record():

    # 获取当前时间戳
    timestamp = int(time.time())
    filepath = f"./audios/asr_{timestamp}.wav"

    if os.path.exists(filepath):
        try:
            os.remove(filepath)
            print(f"Deleted old file: {filepath}")
        except Exception as e:
            print(f"Error deleting old file: {e}")

    # 接收音频数据并保存
    with open(filepath, "wb") as f:
        f.write(request.data)

    segments, info = asr_model.transcribe(filepath, beam_size=5)

    text = "".join([segment.text for segment in segments])

    print("Transcribed text:", text)

    try:
        headers = {
            "Content-Type": "text/plain; charset=utf-8",
            "Content-Length": str(len(text)),
        }
        response = requests.post(
            f"http://%s:8001/upload/text" % client_ip,
            data=text.encode("utf-8"),
            headers=headers,
            timeout=10,
        )
        response.raise_for_status()
        print(f"Sent text to C server. Response status: {response.status_code}")
        print(f"Response text: {response.text}")
    except requests.exceptions.RequestException as e:
        print(f"Error sending text to C server: {e}")

    return "Audio received successfully", 200


@app.route("/send/text", methods=["POST"])
def send_text():

    # 这里可以处理接收到的文本
    text = request.data.decode("utf-8")
    print("Received text:", text)

    # 生成语音
    print("Generating speech...")
    result = chattts_generator.generate(
        text=text + ".",
        voice_name="Default",
        temperature=0.3,
        top_p=0.7,
        top_k=20,
    )

    timestamp = int(time.time())
    filepath = f"./audios/tts_{timestamp}.wav"

    if result:
        sample_rate, audio_data = result
        if save_wav("temp.wav", sample_rate, audio_data):
            if resample_wav("temp.wav", filepath):
                os.remove("temp.wav")
                send_audio_file(filepath, client_ip)
                return "Audio received successfully", 200
            else:
                print("语音重新采样失败")
        else:
            print("语音生成成功，但保存文件失败")
    else:
        print("语音生成失败")

    return "Failed to generate audio", 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, debug=False)

    # text = "现在几点了？"
    # result = chattts_generator.generate(
    #     text=text + ".",
    #     voice_name="Default",
    #     temperature=0.3,
    #     top_p=0.7,
    #     top_k=20,
    # )

    # sample_rate, audio_data = result
    # save_wav("temp.wav", sample_rate, audio_data)
    # resample_wav("temp.wav", "./audios/test.wav")
    # os.remove("temp.wav")
