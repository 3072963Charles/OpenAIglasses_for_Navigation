/*
 * MAX98357A Speaker Test for XIAO ESP32S3 Sense
 *
 * Generates a 440Hz sine wave test tone via I2S.
 * No SD card or audio file needed.
 *
 * ==================== WIRING ====================
 *
 *   XIAO ESP32S3 Sense        MAX98357A
 *   ──────────────────        ─────────
 *   GND                 --->  GND
 *   3.3V                --->  VIN
 *   D1 (GPIO2)          --->  BCLK
 *   D0 (GPIO1)          --->  LRC (WS)
 *   D3 (GPIO4)          --->  DIN
 *   D6 (GPIO43)         --->  SD    (可选: 也可将SD直接接3.3V,代码中将I2S_SD改为-1)
 *   3.3V                --->  GAIN  (12dB增益, 其它增益见下方)
 *
 *   扬声器 + / -        --->  MAX98357A 输出端子 (+ / -)
 *
 * ==================== GAIN 选择 ====================
 *   GAIN 接 GND       -> 9dB
 *   GAIN 悬空          -> 6dB
 *   GAIN 接 VDD(3.3V)  -> 12dB (推荐)
 *   GAIN 经100K到GND   -> 3dB
 *   GAIN 经100K到VDD   -> 15dB
 *
 * ================ XIAO 引脚速查表 =================
 *   XIAO引脚 | GPIO | 备注
 *   D0       |  1   | 空闲,本次用作 LRC
 *   D1       |  2   | 空闲,本次用作 BCLK
 *   D2       |  3   | 共享SD卡CS (勿用)
 *   D3       |  4   | 空闲,本次用作 DOUT
 *   D4       |  5   | I2C SDA
 *   D5       |  6   | I2C SCL
 *   D6       | 43   | UART TX(空闲可用),本次用作 SD
 *   D7       | 44   | UART RX
 *   D8       |  7   | 共享SD卡SCK (勿用)
 *   D9       |  8   | 共享SD卡MISO (勿用)
 *   D10      |  9   | 共享SD卡MOSI (勿用)
 *   板载LED | 21   | 用户LED (低电平亮)
 *   板载PDM麦克风 | 41/42 | 如需释放出来需切断J1/J2焊盘
 * ==================================================
 */

#include <driver/i2s.h>

// --- I2S 引脚定义 (按需修改) ---
#define I2S_BCLK  2    // Bit Clock  -> XIAO D1
#define I2S_LRC   1    // Word Select -> XIAO D0
#define I2S_DOUT  4    // Data Out    -> XIAO D3
#define I2S_SD   43    // Shutdown    -> XIAO D6 (设为-1则直接接3.3V)

// --- 音频参数 ---
#define SAMPLE_RATE     44100
#define TONE_FREQ       440   // A4音符
#define AMPLITUDE       3000  // 0~32767, 留余量避免削波

// --- 内部常量 ---
const int WAVE_TABLE_SIZE = 256;
int16_t sine_table[WAVE_TABLE_SIZE];

void I2S_Init() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  Serial.println("I2S 初始化完成.");
}

void generateSineTable() {
  for (int i = 0; i < WAVE_TABLE_SIZE; i++) {
    sine_table[i] = (int16_t)(AMPLITUDE * sin(2.0 * PI * i / WAVE_TABLE_SIZE));
  }
}

void fillToneBuffer(int16_t* buf, int numFrames) {
  static float phase = 0;
  float phase_step = (float)TONE_FREQ * WAVE_TABLE_SIZE / SAMPLE_RATE;

  for (int i = 0; i < numFrames; i++) {
    int idx = (int)phase % WAVE_TABLE_SIZE;
    int16_t sample = sine_table[idx];
    buf[i * 2] = sample;       // 左声道
    buf[i * 2 + 1] = sample;   // 右声道
    phase += phase_step;
    if (phase >= WAVE_TABLE_SIZE) phase -= WAVE_TABLE_SIZE;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========== MAX98357A 扬声器测试 ==========");

  // 启用功放
  if (I2S_SD >= 0) {
    pinMode(I2S_SD, OUTPUT);
    digitalWrite(I2S_SD, HIGH);
    Serial.printf("SD引脚 GPIO%d 设为高电平 (功放开启)\n", I2S_SD);
  } else {
    Serial.println("SD引脚未使用 (请确保已外接3.3V)");
  }

  // 生成正弦波表
  generateSineTable();
  Serial.printf("正弦波表已生成: %d Hz, 振幅 %d\n", TONE_FREQ, AMPLITUDE);

  // 初始化 I2S
  I2S_Init();
  Serial.printf("采样率: %d Hz\n", SAMPLE_RATE);

  // 播放 10 秒 440Hz 测试音
  const int FRAMES_PER_BUFFER = 256;
  int16_t buf[FRAMES_PER_BUFFER * 2];  // 立体声, 每帧2个采样
  size_t bytes_written;

  Serial.println("正在播放 440Hz 测试音... (持续10秒)");
  Serial.println("如果接线正确,你应该听到连续的蜂鸣声.\n");

  unsigned long start = millis();
  while (millis() - start < 10000) {
    fillToneBuffer(buf, FRAMES_PER_BUFFER);
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
  }

  // 停止并清理
  i2s_zero_dma_buffer(I2S_NUM_0);

  if (I2S_SD >= 0) {
    digitalWrite(I2S_SD, LOW);
    Serial.println("SD引脚设为低电平 (功放关闭)");
  }

  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("测试结束.");
}

void loop() {
  delay(1000);
}
