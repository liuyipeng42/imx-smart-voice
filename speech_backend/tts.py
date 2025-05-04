import numpy as np
import math
from typing import Optional
from pathlib import Path
import sys
import os
import subprocess
import ChatTTS

# 添加必要的路径
sys.path.append(str(Path(__file__).parent.parent))


def float_to_int16(audio: np.ndarray) -> np.ndarray:
    """将浮点音频数据转换为16位整型"""
    am = int(math.ceil(float(np.abs(audio).max())) * 32768)
    am = 32767 * 32768 // am
    return np.multiply(audio, am).astype(np.int16)


class ChatTTSGenerator:
    def __init__(self, custom_model_path: Optional[str] = None):
        """初始化ChatTTS生成器"""
        self.chat = ChatTTS.Chat()
        self.load_model(custom_model_path)

        # 预设音色配置
        self.voice_presets = {
            "Default": {"seed": 2},
            "Timbre1": {"seed": 1111},
            "Timbre2": {"seed": 2222},
            "Timbre3": {"seed": 3333},
            "Timbre4": {"seed": 4444},
            "Timbre5": {"seed": 5555},
            "Timbre6": {"seed": 6666},
            "Timbre7": {"seed": 7777},
            "Timbre8": {"seed": 8888},
            "Timbre9": {"seed": 9999},
        }

        # 使用固定Speaker Embedding和DVAE Coefficient
        self.fixed_spk_emb = "蘁淰敕欀摃誌緘義囡胹讵祪萀梂晳癧亇婇嚕儝揇偩贻咆煴淀蠀欐萵貺箚弃胦菍弁夞皥焆喦卢狧乩夏淔莨臃赽奛溕筡誑緶貿捨讖卢瑫嬅哙硚惣蚵刻玏炉跸徱澾登嬖絢烇嫷媓蔢产虜椪眕俟徊吞詸愣備恍珳湉璑訷珽菹訴痙濽圴謗瘾皡憖啤囊偐惏嶩役磅惃碝贬貇行楝薇磉数綊蟊弤夋荄壪攫撧杶岈硯葳赛悫宸岩稼琜串汏僎灡峂蝇筋茹聈柵焵皿綏缊橥爝澺縬樢訣潙许壚朔仑螽穨糼稰礌漖噍脠庭穪栽嚽袿蟢朁睬筸獸蜍荃俜椉狴掠歾泓葁潚蚗刣悬縶執萏淪肬涼覎培煟苇攁蕘瞥覹緌玽忖熒苼偶巴氶壡卝僕聥栘袴瞗匥弯剫堎搒烅芡渢蒺仉濃猿焳觔吼嚾簬伋諿圀晑牣缄澜枡溒甆欌槙螶璭惝賙扣氒嘕質僜乧畭徉蟖裔既流橊卺奪襾耨嬖脡甆槡巢誸倦訐忂匼俵宰凥覡穰捠斋孖瀤謹讗揲害祩歊蠯旸忎継亍憭徿礯蜷絕凵腂凾疼渴痳旑賧槢浃圕畧晖庞捻翺岊澛縃婳哵喳唗趢咊綼倅佹艅丽趔攪懦蟜牢庨蒘薪蜩煐揈羄获话涴婔傊庪蚫曃氻肙瞥响丹粫璯蕷舺捆搞爳瞻僱潜袄恛懝嗀碥嶎椓一奥濇嵊卂燡懼礅護懭爋蚿檠蟔氖謻淫曇乯槙孓僷疶笺慛誏籜扰固嚲幦吲朸罺眅晝噱簭椼嘎坷嬢粆师恢埨伮跭侂庒瞭幕擛裌藩屙径皎蕾猨徲徎俬渰畣瓂嵭璌砟勗睃沭吾嗅端匈椃棒瓁刉觤伎虗貉柨燜緷奦曛綡拷撮箓縳蠺綢臑栳愆蛴聱嫼亞人翢疋貼横査艼妽菪梷薓棆焉彘撙蝳籯嬎谡毮牥狊垦岩刡趄虾葤纵爩媳泟惏撙剗瓕濂届竨跘匊殱幓你侜羯籕匐璾凡樃俋臺虘蝄懇罶悥孆击捪蛖畋屁蠐蟦埙夬俟抗籵惉柌箼瞀庻勨串捅窮氶賰燧捵蕓汐藈噱臷児汱留翷枾昅想慱羆蚅聢珹礦諅坔嚇缤冫窙蟓壡洦啓茖汬嶉賭汯紡屒揁熀蛾数篧哞撌塔妥蓗懘犌富圃胃莧絗喘葔改脧焛摆儭庥挖謪擾緖蓐卼褟萎磗侻恏嫒愗欮樞羻喻厚欫参姿剝堬絊挒暘擋緷貧妖欷牶诬囌揋膝湷觸柗灚烚誵暡讟卒縉乍跊疥褧皏菈吓穭脓呲挿燐藒澬珹嗧茪芝灲吋崩请瀓蜋棦掙沝刴彸褕缥誐喘胤櫂愄娇肥吥匚佯揔舔瑪燣孲珬谱炆夤梑狕祠痸浾薐萂暟葯俴涊怰蕲眞煍嘷趌褖弹硒囑琋焧截嵨蘈卥呬畸痾厾橓槔赒熰毪稵囨瀺綰穧楳囹籽窷俆坵萵澳瘏穉焬睳洲蓴懬膄揳妦悰尯堇翩葾弉忲昦蟝慎摏衃榶硟兡啥焛堵汼殗搩枌狎斳蒞貼敱叏刳梋莯椥刣吿埓仹熖悲嫿嫤哆怔祸嵢狴斻肎唤樵糪禾瓺摏璂跨卶欢刖薁嬼蚨壳栮余育熪跭讘勖亾擕硬悦痕屺櫞袁椤穟帀㴃"
        self.fixed_dvae_coef = "趺徃詈经嶿垼竸亼减覈聄谨业出觀渿裪伓赢佐嶌媧嫸囁磏疁坤豍蛘燲俰栿箠祓蚒儆嶬瞔櫼蘅竏棩缴赐栦懮觍崿壴溃貁裁嵁蘳绾玚乏讼埰赼椔臠漰舿璧淓褲偏嶿奴蛹襷塏蜧媀跌胲臩腈瀾砫偳豷扫差噓蛼蟨欏蟾皨赓趚懿枒戾檆灃讈觓嶿矽苺晴嚏焁忰讍唛懱艌嬿蛁攳豉荟巼伡櫾埰巏艥襠谂簔臾虅蘿匀贃褁穉嶹聢諽樿穏穈芜趀嚞臫俘琽浦聃讑獌嶔劐諷綵埏蘋薰该嶔臲妷尾塣瑃趖紩左凝盺彼坏纎櫀谭羁凷趑漿犖詣蠈槍嶀嗂滽廗暏聋泘貝亁凷挱谿縔皣聼戅巌滉廻蠃玏滁虨跰焕凧今娽个蠣蟓弧巜嗳諵嚼簏檃玔貪佂臼蒩瀿儱猓譒縸嶀㴁"

    def load_model(self, custom_path: Optional[str] = None) -> bool:
        """加载模型"""
        try:
            if custom_path:
                print(f"Loading local model from: {custom_path}")
                return self.chat.load("custom", custom_path=custom_path, device="cuda")
            else:
                return self.chat.load()
        except Exception as e:
            print(f"Failed to load model: {e}")
            return False

    def generate(
        self,
        text: str,
        voice_name: str = "Default",
        temperature: float = 0.3,
        top_p: float = 0.7,
        top_k: int = 20,
        refine_text: bool = True,
        split_batch: int = 0,
    ) -> Optional[tuple]:
        """
        生成语音

        参数:
            text: 要合成的文本
            voice_name: 音色名称(使用预设音色)
            temperature: 生成温度(0.1-1.0)
            top_p: top-p采样参数(0.5-1.0)
            top_k: top-k采样参数(5-50)
            refine_text: 是否优化文本
            split_batch: 分批处理长文本(0表示不分割)

        返回:
            (采样率, 音频数据) 或 None(失败时)
        """
        if not text.strip():
            print("Warning: Empty input text")
            return None

        try:
            # 1. 文本优化
            if refine_text:
                seed = self.voice_presets.get(
                    voice_name, self.voice_presets["Default"]
                )["seed"]
                text = self.chat.infer(
                    text,
                    skip_refine_text=False,
                    refine_text_only=True,
                    params_refine_text=ChatTTS.Chat.RefineTextParams(
                        temperature=temperature,
                        top_P=top_p,
                        top_K=top_k,
                        manual_seed=seed,
                    ),
                    split_text=split_batch > 0,
                )
                if isinstance(text, list):
                    text = text[0]

            # 2. 生成音频(使用固定Speaker Embedding)
            wav = self.chat.infer(
                text,
                skip_refine_text=True,
                params_infer_code=ChatTTS.Chat.InferCodeParams(
                    spk_emb=self.fixed_spk_emb,
                    temperature=temperature,
                    top_P=top_p,
                    top_K=top_k,
                ),
                stream=False,
                split_text=split_batch > 0,
                max_split_batch=split_batch,
            )

            return 24000, float_to_int16(wav[0]).T

        except Exception as e:
            print(f"Generation failed: {e}")
            return None


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


if __name__ == "__main__":
    # 示例用法
    print("Initializing ChatTTS generator...")
    generator = ChatTTSGenerator("./models/ChatTTS")

    # 生成语音
    print("Generating speech...")
    result = generator.generate(
        text="今天有什么新闻.",
        voice_name="Default",
        temperature=0.3,
        top_p=0.7,
        top_k=20,
    )

    if result:
        sample_rate, audio_data = result
        # 保存为WAV文件
        # ffmpeg -i output.wav -ar 44100 -ac 2 output_resampled.wav
        if save_wav("temp.wav", sample_rate, audio_data):
            if resample_wav("temp.wav", "output.wav"):
                os.remove("temp.wav")
            else:
                print("语音重新采样失败")
        else:
            print("语音生成成功，但保存文件失败")
    else:
        print("语音生成失败")
