/*
 * MAX98357 Driver 使用示例
 * XIAO ESP32S3 Sense + MAX98357A
 *
 * 演示:
 *   1. 初始化驱动
 *   2. 播放 440Hz 方波
 *   3. 调节音量
 *   4. 播放 SD 卡中的 WAV 文件
 */

#include "MAX98357_Driver.h"

// ─── 引脚配置 ──────────────────────────────────────────
// I2S 引脚
#define I2S_BCLK   2     // D1
#define I2S_LRC    1     // D0
#define I2S_DOUT   4     // D3
#define I2S_SD    43     // D6  功放使能

// SD 卡引脚 (XIAO ESP32S3 Sense 板载)
#define SD_CS      3     // D2
#define SD_SCK     7     // D8
#define SD_MISO    8     // D9
#define SD_MOSI    9     // D10

// 音乐文件路径
#define MUSIC_FILE "/music.wav"

// ─── 全局驱动实例 ──────────────────────────────────────
MAX98357 audio;

// ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n===== MAX98357 Driver Demo =====");

  // 1. 初始化
  Serial.print("初始化... ");
  if (audio.init(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_SD)) {
    Serial.println("OK");
  } else {
    Serial.println("FAIL");
    while (1) delay(1000);
  }

  // 2. 播放 440Hz 方波 2 秒 (50% 音量)
  Serial.println("播放 440Hz 方波 (2秒)...");
  audio.setVolume(50);
  audio.playTone(440, 2000);
  delay(500);

  // 3. 更大声
  Serial.println("播放 880Hz 方波 (1秒, 30%音量)...");
  audio.setVolume(30);
  audio.playTone(880, 1000);
  delay(500);

  // 4. 播放 WAV 音乐文件
  Serial.println("播放音乐文件...");
  if (!audio.playFile(MUSIC_FILE, SD_CS, SD_SCK, SD_MISO, SD_MOSI)) {
    Serial.println("音乐播放失败, 请检查 SD 卡和文件路径.");
  }

  Serial.println("Demo 结束.");
}

void loop() {
  delay(1000);
}
