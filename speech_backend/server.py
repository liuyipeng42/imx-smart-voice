from flask import Flask, request, jsonify
from faster_whisper import WhisperModel
import requests
import time
import os
import tts


asr_model = WhisperModel(
    "./models/faster-whisper-large-v3", device="cuda", compute_type="int8_float16"
)

chattts_generator = tts.ChatTTSGenerator("./models/ChatTTS")

app = Flask(__name__)


@app.route("/upload_record", methods=["POST"])
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
        headers = {"Content-Type": "text/plain; charset=utf-8"}
        response = requests.post(
            "http://10.33.103.61:8001",
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


@app.route("/send_text", methods=["GET"])
def send_text():
    # 这里可以处理接收到的文本
    text = request.args.get("text")
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
            else:
                print("语音重新采样失败")
        else:
            print("语音生成成功，但保存文件失败")
    else:
        print("语音生成失败")


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, debug=False)
