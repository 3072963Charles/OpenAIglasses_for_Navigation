// ===== XIAO ESP32S3 Sense — 摄像头硬件连接全面诊断 =====
// 烧录后开串口 115200，按顺序看完所有检测结果

#include <Wire.h>
#include <esp_camera.h>

// 摄像头引脚（XIAO ESP32S3 Sense 标准定义）
#define PWDN  -1
#define RESET -1
#define XCLK  10
#define SIOD  40
#define SIOC  39
#define Y2    15
#define Y3    17
#define Y4    18
#define Y5    16
#define Y6    14
#define Y7    12
#define Y8    11
#define Y9    48
#define VSYNC 38
#define HREF  47
#define PCLK  13

// ====================================================================
// 测试1: 基本系统信息
// ====================================================================
void test1_system_info() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 1: 系统基础信息              ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  Serial.printf("  芯片: %s rev%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("  Flash: %u MB\n", ESP.getFlashChipSize() / 1048576);
  Serial.printf("  PSRAM: %u MB\n", ESP.getPsramSize() / 1048576);
  Serial.printf("  可用 Heap: %u KB\n", ESP.getFreeHeap() / 1024);

  if (ESP.getPsramSize() == 0) {
    Serial.println("  ❌ PSRAM 未启用！请在 Arduino IDE: Tools→PSRAM→OPI PSRAM");
  } else {
    Serial.println("  ✅ PSRAM OK");
  }
}

// ====================================================================
// 测试2: GPIO 39/40 (SCCB引脚) 基本功能测试
//   看这两个引脚是否能正常拉高拉低，排除引脚烧毁/短路
// ====================================================================
void test2_gpio_basic() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 2: SCCB 引脚 (GPIO 39,40)  ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  // 注意：GPIO 39 在 ESP32-S3 上是 RTC GPIO，可作为输入输出使用
  // 先做基本的数字读写测试（即使最后要还给 I2C）

  // --- GPIO 40 (SIOD/SDA) ---
  pinMode(SIOD, OUTPUT);
  digitalWrite(SIOD, HIGH);
  delay(1);
  int v40_h = digitalRead(SIOD);
  digitalWrite(SIOD, LOW);
  delay(1);
  int v40_l = digitalRead(SIOD);
  pinMode(SIOD, INPUT_PULLUP);
  delay(1);
  int v40_pu = digitalRead(SIOD);
  pinMode(SIOD, INPUT_PULLDOWN);
  delay(1);
  int v40_pd = digitalRead(SIOD);

  Serial.printf("  GPIO 40 (SIOD): HIGH=%d  LOW=%d  PU=%d  PD=%d\n", v40_h, v40_l, v40_pu, v40_pd);

  bool ok40 = (v40_h == 1 && v40_l == 0 && v40_pu == 1 && v40_pd == 0);
  Serial.printf("  → GPIO 40: %s\n", ok40 ? "✅ 正常" : "⚠️ 异常(可能短路/浮空)");

  // --- GPIO 39 (SIOC/SCL) ---
  pinMode(SIOC, OUTPUT);
  digitalWrite(SIOC, HIGH);
  delay(1);
  int v39_h = digitalRead(SIOC);
  digitalWrite(SIOC, LOW);
  delay(1);
  int v39_l = digitalRead(SIOC);
  pinMode(SIOC, INPUT_PULLUP);
  delay(1);
  int v39_pu = digitalRead(SIOC);
  pinMode(SIOC, INPUT_PULLDOWN);
  delay(1);
  int v39_pd = digitalRead(SIOC);

  Serial.printf("  GPIO 39 (SIOC): HIGH=%d  LOW=%d  PU=%d  PD=%d\n", v39_h, v39_l, v39_pu, v39_pd);

  bool ok39 = (v39_h == 1 && v39_l == 0 && v39_pu == 1 && v39_pd == 0);
  Serial.printf("  → GPIO 39: %s\n", ok39 ? "✅ 正常" : "⚠️ 异常(可能短路/浮空)");

  // --- GPIO 10 (XCLK) ---
  pinMode(XCLK, OUTPUT);
  digitalWrite(XCLK, HIGH);
  delay(1);
  int v10_h = digitalRead(XCLK);
  digitalWrite(XCLK, LOW);
  delay(1);
  int v10_l = digitalRead(XCLK);
  Serial.printf("  GPIO 10 (XCLK): HIGH=%d  LOW=%d → %s\n", v10_h, v10_l,
                (v10_h==1 && v10_l==0) ? "✅ 正常" : "⚠️ 异常");
}

// ====================================================================
// 测试3: I2C 扫描 — 多种频率、上拉配置
// ====================================================================
void test3_i2c_scan_all() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 3: I2C 总线扫描 (全部组合)  ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  uint32_t speeds[] = {10000, 50000, 100000, 200000, 400000};
  const char* speed_labels[] = {"10kHz", "50kHz", "100kHz", "200kHz", "400kHz"};

  struct {
    bool pullup;
    const char* label;
  } pull_cfgs[] = {
    { false, "无上拉" },
    { true,  "内部上拉" },
  };

  int total_found = 0;

  for (auto& pull : pull_cfgs) {
    for (int si = 0; si < 5; si++) {
      // 重新配置 Wire，设定是否需要内部上拉
      Wire.end();
      delay(10);

      if (pull.pullup) {
        // 带内部上拉的 I2C
        pinMode(SIOD, INPUT_PULLUP);
        pinMode(SIOC, INPUT_PULLUP);
        Wire.begin(SIOD, SIOC);
      } else {
        Wire.begin(SIOD, SIOC);
      }

      Wire.setClock(speeds[si]);
      delay(10);

      Serial.printf("\n  [%s + %s]", pull.label, speed_labels[si]);

      int found = 0;
      for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          if (found == 0) Serial.println();
          Serial.printf("    ✅ 0x%02X (%d)", addr, addr);

          if (addr == 0x30)      Serial.print(" ← 典型 OV2640");
          else if (addr == 0x3C) Serial.print(" ← 典型 OV3660/OV5640");
          else if (addr == 0x21) Serial.print(" ← 典型 OV7725/GC032A");
          Serial.println();

          found++;
        }
      }
      if (found == 0) {
        Serial.print(" ❌ 无设备");
      }
      Serial.println();
      total_found += found;

      // 超低频率再加试一次
      if (speeds[si] == 10000 && found == 0) {
        Wire.setClock(5000);
        delay(10);
        Serial.printf("  [%s + 5kHz 超低速]", pull.label);
        int f2 = 0;
        for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
          Wire.beginTransmission(addr);
          if (Wire.endTransmission() == 0) {
            Serial.printf("\n    ✅ 0x%02X", addr);
            f2++;
          }
        }
        if (f2 == 0) Serial.print(" ❌ 仍无设备");
        Serial.println();
        total_found += f2;
      }
    }
  }

  Serial.println();
  if (total_found == 0) {
    Serial.println("  ┌─────────────────────────────────────┐");
    Serial.println("  │ ❌ 所有组合均无设备 → 硬件层面不通 │");
    Serial.println("  │                                    │");
    Serial.println("  │  可能原因:                         │");
    Serial.println("  │  1. 排线触点面插反                 │");
    Serial.println("  │  2. 排线未卡紧 (FPC座卡扣没锁)    │");
    Serial.println("  │  3. 排线有断裂                     │");
    Serial.println("  │  4. FPC 座虚焊/脱焊               │");
    Serial.println("  │  5. 摄像头模组本身故障             │");
    Serial.println("  └─────────────────────────────────────┘");
  }
}

// ====================================================================
// 测试4: 读取摄像头并行数据引脚状态
//   如果摄像头通电但无 XCLK，数据引脚应该有偏置电压（约 1.2-1.8V）
//   全部为 0 或全部为 3.3V 都说明有问题
// ====================================================================
void test4_data_pins() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 4: 数据引脚静态电平检测      ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  Serial.println("  将全部摄像头引脚设为 INPUT_PULLDOWN，读取静态电平...");

  int data_pins[] = {Y2, Y3, Y4, Y5, Y6, Y7, Y8, Y9, PCLK, VSYNC, HREF};
  const char* names[]   = {"Y2","Y3","Y4","Y5","Y6","Y7","Y8","Y9","PCLK","VSYNC","HREF"};

  int high_count = 0;
  int low_count  = 0;

  for (int i = 0; i < 11; i++) {
    pinMode(data_pins[i], INPUT_PULLDOWN);
    delay(1);
    int v = digitalRead(data_pins[i]);
    Serial.printf("  %-6s (GPIO %2d): %s\n", names[i], data_pins[i], v ? "HIGH" : "LOW");
    if (v) high_count++; else low_count++;
  }

  Serial.println();
  if (high_count > 0) {
    Serial.println("  📝 有引脚为 HIGH → 摄像头可能已上电、有偏置电压");
  }
  if (low_count == 11) {
    Serial.println("  📝 全部引脚为 LOW → 摄像头可能完全没通电（排线完全断开）");
  }
  if (high_count == 11) {
    Serial.println("  ⚠️ 全部为 HIGH → 可能有短路/外部上拉异常");
  }
}

// ====================================================================
// 测试5: 尝试完整摄像头初始化 (不加 WiFi)
// ====================================================================
void test5_full_camera_init() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 5: 完整摄像头初始化 (裸)    ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  // 先打印 esp_camera 库版本信息（如果有的话）
  // 注意：esp_camera_init 内部做了:
  //   1. 配置 LEDC 产生 XCLK
  //   2. 配置 SCCB (I2C)
  //   3. 探测传感器 → 读 ID
  //   4. 如果 ID 不匹配 → 0x106

  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0  = Y2;  cfg.pin_d1  = Y3;
  cfg.pin_d2  = Y4;  cfg.pin_d3  = Y5;
  cfg.pin_d4  = Y6;  cfg.pin_d5  = Y7;
  cfg.pin_d6  = Y8;  cfg.pin_d7  = Y9;
  cfg.pin_xclk = XCLK;   cfg.pin_pclk = PCLK;
  cfg.pin_vsync = VSYNC; cfg.pin_href  = HREF;
  cfg.pin_sscb_sda = SIOD;
  cfg.pin_sscb_scl = SIOC;
  cfg.pin_pwdn  = PWDN;
  cfg.pin_reset = RESET;

  // 分别用两个常见频率尝试
  uint32_t freqs[] = {20000000, 10000000};
  const char* flabels[] = {"20MHz(OV2640)", "10MHz(OV3660)"};

  for (int i = 0; i < 2; i++) {
    Serial.printf("\n  >>> 尝试: XCLK=%lu Hz (%s)\n", freqs[i], flabels[i]);

    cfg.xclk_freq_hz   = freqs[i];
    cfg.pixel_format    = PIXFORMAT_JPEG;
    cfg.frame_size      = FRAMESIZE_QVGA;
    cfg.jpeg_quality    = 12;
    cfg.fb_count        = 1;
    cfg.fb_location     = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode       = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&cfg);

    if (err == ESP_OK) {
      sensor_t* s = esp_camera_sensor_get();
      if (s) {
        Serial.printf("  ✅ 成功! 传感器 PID=0x%02X, VER=0x%02X, MIDH=0x%02X, MIDL=0x%02X\n",
                      s->id.PID, s->id.VER, s->id.MIDH, s->id.MIDL);

        // 识别型号
        if (s->id.PID == 0x26) Serial.println("  → 型号: OV2640");
        else if (s->id.PID == 0x36) Serial.println("  → 型号: OV3660");
        else if (s->id.PID == 0x56) Serial.println("  → 型号: OV5640");
        else Serial.printf("  → 未知型号, PID=0x%02X\n", s->id.PID);

        // 抓一帧
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
          Serial.printf("  ✅ 抓帧: %u bytes, %dx%d\n", fb->len, fb->width, fb->height);
          esp_camera_fb_return(fb);
        } else {
          Serial.println("  ⚠️ 抓帧失败（初始化成功但抓图失败）");
        }
      }

      esp_camera_deinit();
      delay(100);
      return;  // 成功了就不需要再试
    } else {
      Serial.printf("  ❌ err=0x%x", err);
      if (err == 0x102) Serial.print(" (ESP_ERR_INVALID_ARG)");
      else if (err == 0x105) Serial.print(" (ESP_ERR_NOT_FOUND - 无设备)");
      else if (err == 0x106) Serial.print(" (ESP_ERR_NOT_SUPPORTED - 不认识)");
      Serial.println();
    }
  }

  Serial.println("\n  ┌─────────────────────────────────────────┐");
  Serial.println("  │ 总结: esp_camera_init 全部失败         │");
  Serial.println("  │                                         │");
  Serial.println("  │ 如果 Test 3 也扫不到 I2C 设备:         │");
  Serial.println("  │ → 排线物理连接问题 (大概率)            │");
  Serial.println("  │                                         │");
  Serial.println("  │ 如果 Test 3 能扫到但 init 失败:        │");
  Serial.println("  │ → 传感器型号不在驱动支持列表           │");
  Serial.println("  └─────────────────────────────────────────┘");
}

// ====================================================================
// 测试6: 用普通 GPIO 模拟 I2C 扫描 (bit-bang)
//   排除 Wire 库自身的 bug 或配置问题
// ====================================================================
void test6_bitbang_scan() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println(  "║  TEST 6: Bit-bang I2C 扫描         ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  // 软件模拟 I2C START
  auto i2c_start = []() {
    pinMode(SIOD, OUTPUT);
    pinMode(SIOC, OUTPUT);
    digitalWrite(SIOD, HIGH);
    digitalWrite(SIOC, HIGH);
    delayMicroseconds(5);
    digitalWrite(SIOD, LOW);
    delayMicroseconds(5);
    digitalWrite(SIOC, LOW);
  };

  // 软件模拟 I2C STOP
  auto i2c_stop = []() {
    pinMode(SIOD, OUTPUT);
    digitalWrite(SIOC, LOW);
    digitalWrite(SIOD, LOW);
    delayMicroseconds(5);
    digitalWrite(SIOC, HIGH);
    delayMicroseconds(5);
    digitalWrite(SIOD, HIGH);
    delayMicroseconds(5);
  };

  // 发送一个字节, 返回 ACK (true=有应答)
  auto i2c_write = [](uint8_t byte) -> bool {
    pinMode(SIOD, OUTPUT);
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(SIOD, (byte >> bit) & 0x01);
      delayMicroseconds(2);
      digitalWrite(SIOC, HIGH);
      delayMicroseconds(5);
      digitalWrite(SIOC, LOW);
      delayMicroseconds(2);
    }
    // 释放 SDA, 读 ACK
    pinMode(SIOD, INPUT_PULLUP);
    delayMicroseconds(2);
    digitalWrite(SIOC, HIGH);
    delayMicroseconds(5);
    int ack = digitalRead(SIOD);  // LOW=ACK, HIGH=NACK
    digitalWrite(SIOC, LOW);
    delayMicroseconds(2);
    return (ack == 0);
  };

  int found = 0;
  for (int addr = 0x01; addr < 0x7F; addr++) {
    i2c_start();
    bool ack = i2c_write((uint8_t)(addr << 1) | 0); // 写地址
    i2c_stop();
    delayMicroseconds(50);

    if (ack) {
      Serial.printf("  ✅ Bit-bang 发现设备: 0x%02X (%d)\n", addr, addr);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("  ❌ Bit-bang 也未发现设备");
    Serial.println("  → Wire 库没问题，确实是硬件层不通");
  } else {
    Serial.println("  ⚠️ Bit-bang 发现了设备但 Wire 扫描没发现！");
    Serial.println("  → Wire 库配置/时序有问题");
  }
}

// ====================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n╔══════════════════════════════════════════════╗");
  Serial.println(  "║                                              ║");
  Serial.println(  "║   摄像头硬件连接全面诊断                     ║");
  Serial.println(  "║   XIAO ESP32S3 Sense                        ║");
  Serial.println(  "║                                              ║");
  Serial.println(  "╚══════════════════════════════════════════════╝");

  test1_system_info();
  test2_gpio_basic();
  test3_i2c_scan_all();
  test4_data_pins();
  test6_bitbang_scan();
  test5_full_camera_init();

  // ===== 最终结论 =====
  Serial.println("\n\n╔══════════════════════════════════════════════╗");
  Serial.println(  "║              诊 断 结 束                     ║");
  Serial.println(  "╚══════════════════════════════════════════════╝");
  Serial.println();
  Serial.println("  请把以上完整输出复制/截图");
  Serial.println("  重点关注:");
  Serial.println("  - Test 2: GPIO 39/40 是否正常 (HIGH=1, LOW=0)");
  Serial.println("  - Test 3: I2C 有无设备");
  Serial.println("  - Test 6: Bit-bang 有无设备");
  Serial.println();
  Serial.println("  Test 3/6 都没设备 → 排线问题（方向/卡扣/断裂）");
  Serial.println("  Test 2 异常       → GPIO 引脚损坏");
  Serial.println("  Test 3 无但 6 有  → Wire 库问题");
  Serial.println();
}

void loop() {
  delay(60000);
}
