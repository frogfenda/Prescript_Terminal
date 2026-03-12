// 文件：src/prescript_data.cpp
#include "prescript_data.h"

// ==========================================
// 英文都市指令库 (English Prescripts)
// ==========================================
static const char* prescripts_en[] = {
   "To... the one who has just opened their eyes, eat yesterday's leftover soup while standing at the kitchen counter. Do not heat it.",
    "To... the person wearing a red tie today, at precisely 3:33 PM, pour a glass of water onto the soil of the nearest potted plant. Say nothing.",
    "To... the third customer to enter the bakery on Elm Street, purchase seven loaves of bread. Consume the first slice of the seventh loaf before exiting. Dispose of the remainder in the blue dumpster in the alley.",
    "To... the one reading this note, find the oldest book on your shelf. Tear out page 22 and fold it into a paper boat. Set it afloat in the nearest public fountain before sunset.",
    "To... the first person you make eye contact with after receiving this, ask them for the time. Regardless of their answer, respond with: 'The time is always correct for the needle.'",
    "To... the bearer, do not speak for the next hour. Communicate only through gestures. If you fail, cut a lock of your own hair.",
    "To... the resident of apartment 4B, tonight, place a single lit candle on your windowsill facing north. Let it burn out completely.",
    "To... the one with ink-stained fingers, write the word 'OBEDIENCE' one hundred times on a sheet of paper. Burn the paper upon completion. Inhale the smoke.",
    "To... the commuter on the 8:15 train, offer your seat to the tallest individual in the carriage. Remain standing for the duration of your journey.",
    "To... the one who feels doubt, walk to the nearest crossroads. Spin three times counterclockwise. Proceed in the direction you are facing for exactly 100 paces.",
    "To... the listener, the next song you hear, memorize its final lyric. Whisper it to the first stray animal you encounter.",
    "To... the cook, prepare a meal using only ingredients that are blue or purple. You must finish it entirely alone.",
    "To... the dreamer, stay awake until the clock's hour and minute hands overlap. At that moment, prick your finger and mark this note with a drop of blood.",
    "To... the witness, find a mirror in a public restroom. Look into your own eyes for two full minutes without blinking. If you blink, break the mirror.",
    "To... the holder of a copper coin, flip it at a bus stop. If it lands heads, board the next bus. If tails, walk in the opposite direction of its route for 30 minutes.",
    "To... the one with a silent phone, call the last number in your call history. Do not speak. Listen for 30 seconds, then hang up regardless.",
    "To... the wearer of black shoes, at noon, stand on the highest elevation in your current district. Face east and remain perfectly still for 180 seconds.",
    "To... the one who receives this via terminal, disconnect from the network for the next 24 hours. If you reconnect prematurely, format your primary drive.",
    "To... the first-born, find a photograph of your younger self. Hold it over a flame until the edges curl. Keep the ashes in your pocket for the rest of the day.",
    "To... the one seeking warmth, extinguish all artificial heat sources in your dwelling for the next night. Use only a single blanket.",
    "To... the one who speaks too much, thread a needle with red thread. Sew your lips shut metaphorically by piercing the collar of your garment seven times while silent.",
    "To... the observer, find a window with a view of a street. Count the number of people wearing hats. If the number is even, smile. If odd, frown. Maintain the expression for one hour.",
    "To... the keeper of secrets, write your greatest fear on a slip of paper. Fold it, place it under a rock in a nearby park. Never return to that rock.",
    "To... the one ascending stairs, skip every third step. If you miss your count, return to the bottom and begin again.",
    "To... the person near flowing water, drop a valuable possession into the current. Do not retrieve it."
};

// ==========================================
// 中文都市指令库 (Chinese Prescripts)
// ==========================================
// 【重要提示】：由于目前的排版动画算法是按 20 个字节硬切的。
// 一个汉字占 3 个字节，如果长句子不加空格，硬切时可能会把汉字劈成两半导致乱码。
// 临时解决方案：在中文长句中加入空格，引导算法在空格处安全换行。
static const char* prescripts_zh[] = {
     "致... 刚刚睁开眼睛的人，站在厨房柜台前吃昨天的剩汤。不要加热。",
    "致... 今天戴红色领带的人，下午3:33整，将一杯水倒在最近盆栽的土壤上。不要说话。",
    "致... 进入榆树街面包店的第三位顾客，购买七条面包。在离开前吃掉第七条面包的第一片。将剩余部分丢弃在巷子的蓝色垃圾桶中。",
    "致... 阅读此便条的人，找到你书架上最旧的书。撕下第22页，将其折成纸船。在日落前将其放入最近的公共喷泉中漂流。",
    "致... 收到此指令后与你目光相对的第一个人，向他们询问时间。无论他们如何回答，回应说：'时针所指总是正确之时。'",
    "致... 持此令者，接下来一小时内不要说话。仅通过手势交流。如果你失败，剪下自己的一绺头发。",
    "致... 4B公寓的住户，今晚，在你朝北的窗台上放置一支点燃的蜡烛。让它完全燃尽。",
    "致... 手指沾有墨水的人，在一张纸上写一百遍'服从'这个词。完成后烧掉纸张。吸入烟雾。",
    "致... 8:15列车的通勤者，将你的座位让给车厢里最高的人。在你的整个旅程中保持站立。",
    "致... 心存疑虑者，走到最近的十字路口。逆时针旋转三圈。然后朝你面对的方向正好走100步。",
    "致... 聆听者，记住你听到的下一首歌的最后一句歌词。低声告诉遇到的第一只流浪动物。",
    "致... 厨师，仅使用蓝色或紫色的食材准备一餐。你必须独自吃完。",
    "致... 梦想家，保持清醒直到时钟的时针和分针重叠。在那一刻，刺破你的手指，用一滴血标记此便条。",
    "致... 见证者，在公共洗手间找到一面镜子。直视自己的眼睛两分钟不眨眼。如果你眨眼了，打碎镜子。",
    "致... 持有一枚铜币的人，在公交车站抛掷它。如果正面朝上，登上下一班公交车。如果反面朝上，朝公交车路线的相反方向行走30分钟。",
    "致... 手机静默者，拨打你通话记录中的最后一个号码。不要说话。倾听30秒，然后无论如何挂断电话。",
    "致... 穿着黑色鞋子的人，中午时分，站在你所在区域的最高点。面朝东方，保持完全静止180秒。",
    "致... 通过终端收到此指令的人，断开网络连接24小时。如果你提前重新连接，格式化你的主硬盘。",
    "致... 头胎所生者，找到一张你年轻时的照片。将其举在火焰上直到边缘卷曲。将灰烬放在口袋里一整天。",
    "致... 寻求温暖者，熄灭你住所中所有人工热源一晚。只使用一条毯子。",
    "致... 言语过多者，用红线穿针。在沉默中，将你的衣领穿刺七次，以此象征性地缝上你的嘴唇。",
    "致... 观察者，找一扇能看见街道的窗户。数一数戴帽子的人数。如果是偶数，微笑。如果是奇数，皱眉。保持这个表情一小时。",
    "致... 秘密守护者，将你最大的恐惧写在一张小纸条上。折好，放在附近公园的一块石头下。永远不要回到那块石头旁。",
    "致... 上楼梯者，跳过每三级台阶。如果你数错了，返回底部重新开始。",
    "致... 靠近流水者，将一件有价值的物品丢入水流中。不要取回。"
};

int Get_Prescript_Count(SystemLang_t lang) {
    if (lang == LANG_ZH) {
        return sizeof(prescripts_zh) / sizeof(prescripts_zh[0]);
    } else {
        return sizeof(prescripts_en) / sizeof(prescripts_en[0]);
    }
}

const char* Get_Prescript(SystemLang_t lang, int index) {
    if (lang == LANG_ZH) {
        return prescripts_zh[index];
    } else {
        return prescripts_en[index];
    }
}