/*
 * WAV Music Player - XIAO ESP32S3 Sense + MAX98357A
 *
 * 从 microSD 卡读取 WAV 文件，通过 I2S 播放到 MAX98357A 扬声器。
 * 支持 8/16 bit, mono/stereo, 常见采样率 (8000 ~ 96000 Hz)。
 *
 * ==================== WIRING ====================
 *
 *   XIAO ESP32S3 Sense        MAX98357A
 *   ──────────────────        ─────────
 *   GND                --->   GND
 *   3.3V               --->   VIN
 *   D1 (GPIO2)         --->   BCLK
 *   D0 (GPIO1)         --->   LRC (WS)
 *   D3 (GPIO4)         --->   DIN
 *   D6 (GPIO43)        --->   SD
 *   3.3V               --->   GAIN (12dB)
 *
 *   SD 卡槽为 XIAO ESP32S3 Sense 板载，无需额外接线。
 *
 * ==================== 使用方法 ====================
 *   1. 将 WAV 文件拷贝到 microSD 卡根目录
 *   2. 修改下方 FILENAME 为你的文件名 (如 "song.wav")
 *   3. 插入 SD 卡，上传程序
 *   4. 打开串口监视器 (115200) 查看播放信息
 *
 * ==================== 支持的格式 ====================
 *   - WAV (PCM), 8bit 或 16bit
 *   - 单声道或立体声
 *   - 常用采样率: 8000, 11025, 16000, 22050, 44100, 48000 等
 */

#include <driver/i2s.h>
#include <FS.h>
#include <SD.h>

// --- SD 卡引脚 (XIAO ESP32S3 Sense 板载卡槽) ---
#define SD_CS    3    // D2
#define SD_SCK   7    // D8
#define SD_MISO  8    // D9
#define SD_MOSI  9    // D10

// --- I2S 引脚 ---
#define I2S_BCLK  2   // D1
#define I2S_LRC   1   // D0
#define I2S_DOUT  4   // D3
#define I2S_SD   43   // D6 (功放使能, 设为 -1 则直接接 3.3V)

// --- 播放设置 ---
#define FILENAME  "/music.wav"   // SD 卡根目录下的 WAV 文件名
#define DMA_BUF_COUNT  8
#define DMA_BUF_LEN    1024

// WAV 文件头结构
struct WavHeader {
  char     riff[4];         // "RIFF"
  uint32_t fileSize;
  char     wave[4];         // "WAVE"
  char     fmt[4];          // "fmt "
  uint32_t fmtSize;
  uint16_t audioFormat;     // 1 = PCM
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
};

struct ChunkHeader {
  char     id[4];
  uint32_t size;
};

// 全局变量
static i2s_port_t i2s_port = I2S_NUM_0;
static int i2s_bps;   // bits per sample for I2S config

// --- 初始化 I2S ---
void I2S_Init(int sampleRate, int bitsPerSample, int channels) {
  // I2S 位宽: 8bit 用 16bit 传输(兼容), 16/24/32bit 直接使用
  i2s_bits_per_sample_t bps = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_bps = 16;

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (uint32_t)sampleRate,
    .bits_per_sample = bps,
    .channel_format = (channels == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN
  };

  i2s_pin_config_t pin_cfg = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(i2s_port, &cfg, 0, NULL);
  i2s_set_pin(i2s_port, &pin_cfg);
  i2s_set_clk(i2s_port, sampleRate, bps, (channels == 2) ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);
}

// --- 查找 data chunk ---
bool findDataChunk(File &file, uint32_t *dataSize) {
  ChunkHeader chunk;

  // 跳过 RIFF 头部 (12 bytes), 从第一个子块开始搜索
  file.seek(12);

  while (file.available()) {
    if (file.read((uint8_t*)&chunk, sizeof(ChunkHeader)) != sizeof(ChunkHeader)) {
      return false;
    }

    if (memcmp(chunk.id, "data", 4) == 0) {
      *dataSize = chunk.size;
      return true;
    }

    // 跳到下一个 chunk
    file.seek(file.position() + chunk.size);
  }
  return false;
}

// --- 8bit 转 16bit (8bit WAV 是无符号的, I2S 需要带符号的 16bit) ---
void convert8to16(uint8_t *in, int16_t *out, int len) {
  for (int i = 0; i < len; i++) {
    out[i] = ((int16_t)in[i] - 128) << 8;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========== WAV Music Player ==========");

  // --- 启用功放 ---
  if (I2S_SD >= 0) {
    pinMode(I2S_SD, OUTPUT);
    digitalWrite(I2S_SD, HIGH);
    Serial.printf("功放已开启 (GPIO%d HIGH)\n", I2S_SD);
  }

  // --- 初始化 SD 卡 ---
  Serial.print("初始化 SD 卡... ");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("失败!");
    Serial.println("请检查: 1) SD卡是否插入 2) 是否格式化为 FAT32");
    while (1) { delay(1000); }
  }
  Serial.println("OK");

  // --- 打开 WAV 文件 ---
  Serial.printf("打开文件: %s\n", FILENAME);
  File file = SD.open(FILENAME);
  if (!file) {
    Serial.println("文件打开失败! 请检查文件名是否正确.");
    while (1) { delay(1000); }
  }
  Serial.printf("文件大小: %d bytes\n", file.size());

  // --- 解析 WAV 头 ---
  WavHeader header;
  file.read((uint8_t*)&header, sizeof(WavHeader));

  if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
    Serial.println("不是有效的 WAV 文件!");
    file.close();
    while (1) { delay(1000); }
  }

  if (header.audioFormat != 1) {
    Serial.printf("不支持的格式: %d (仅支持 PCM)\n", header.audioFormat);
    file.close();
    while (1) { delay(1000); }
  }

  // 打印文件信息
  Serial.println("\n--- 音频信息 ---");
  Serial.printf("采样率:     %d Hz\n", header.sampleRate);
  Serial.printf("位深度:     %d bit\n", header.bitsPerSample);
  Serial.printf("声道数:     %d (%s)\n", header.numChannels,
                header.numChannels == 2 ? "立体声" : "单声道");
  Serial.printf("字节率:     %d B/s\n", header.byteRate);
  Serial.println("------------------");

  // --- 查找 data chunk ---
  file.seek(sizeof(WavHeader));
  uint32_t dataSize;
  if (!findDataChunk(file, &dataSize)) {
    Serial.println("找不到 data chunk!");
    file.close();
    while (1) { delay(1000); }
  }
  Serial.printf("音频数据: %d bytes (%.1f 秒)\n\n", dataSize,
                (float)dataSize / header.byteRate);

  // --- 初始化 I2S ---
  I2S_Init(header.sampleRate, header.bitsPerSample, header.numChannels);

  // --- 播放 ---
  Serial.println("开始播放...");

  const int BUF_SAMPLES = DMA_BUF_LEN;
  uint8_t  rawBuf[BUF_SAMPLES * 2];        // 原始数据缓冲
  int16_t  outBuf[BUF_SAMPLES * 2];         // 输出数据缓冲 (16bit stereo)
  size_t   bytesWritten;
  uint32_t bytesRemaining = dataSize;
  uint32_t totalSent = 0;

  unsigned long startTime = millis();

  while (bytesRemaining > 0) {
    // 每次读取的量
    int sampleWidth = header.bitsPerSample / 8;        // 每采样字节数
    int toRead = min((uint32_t)BUF_SAMPLES * header.numChannels * sampleWidth, bytesRemaining);
    int samplesInBuffer = toRead / (header.numChannels * sampleWidth);

    int bytesRead = file.read(rawBuf, toRead);
    if (bytesRead <= 0) break;

    if (header.bitsPerSample == 8) {
      // 8bit -> 16bit
      convert8to16(rawBuf, outBuf, bytesRead);
      // 如果是单声道, 复制到左右声道
      if (header.numChannels == 1) {
        for (int i = bytesRead - 1; i >= 0; i--) {
          outBuf[i * 2] = outBuf[i];
          outBuf[i * 2 + 1] = outBuf[i];
        }
        i2s_write(i2s_port, outBuf, bytesRead * 4, &bytesWritten, portMAX_DELAY);
      } else {
        i2s_write(i2s_port, outBuf, bytesRead * 2, &bytesWritten, portMAX_DELAY);
      }
    } else {
      // 16bit
      if (header.numChannels == 1) {
        // 单声道 → 扩为立体声
        int16_t *src = (int16_t*)rawBuf;
        for (int i = bytesRead / 2 - 1; i >= 0; i--) {
          outBuf[i * 2] = src[i];
          outBuf[i * 2 + 1] = src[i];
        }
        i2s_write(i2s_port, outBuf, bytesRead * 2, &bytesWritten, portMAX_DELAY);
      } else {
        i2s_write(i2s_port, rawBuf, bytesRead, &bytesWritten, portMAX_DELAY);
      }
    }

    totalSent += bytesRead;
    bytesRemaining -= bytesRead;
  }

  unsigned long elapsed = millis() - startTime;
  float actualDuration = (float)dataSize / header.byteRate;

  Serial.printf("播放完成! 实际用时: %.1f 秒 (理论: %.1f 秒)\n",
                elapsed / 1000.0, actualDuration);

  // --- 清理 ---
  i2s_zero_dma_buffer(i2s_port);
  file.close();

  if (I2S_SD >= 0) {
    digitalWrite(I2S_SD, LOW);
  }
  i2s_driver_uninstall(i2s_port);
  Serial.println("结束.");
}

void loop() {
  delay(1000);
}
